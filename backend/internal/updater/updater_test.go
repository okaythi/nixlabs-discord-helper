package updater

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestCompareVersions(t *testing.T) {
	cases := []struct {
		v1, v2 string
		want   int
	}{
		{"1.0.15", "1.0.14", 1},
		{"v1.0.15", "1.0.14", 1},
		{"1.0.14", "v1.0.14", 0},
		{"1.0.9", "1.0.10", -1},
		{"2.0.0", "1.99.99", 1},
		{"1.1", "1.0.9", 1},
	}
	for _, tc := range cases {
		if got := CompareVersions(tc.v1, tc.v2); got != tc.want {
			t.Errorf("CompareVersions(%q, %q) = %d, want %d", tc.v1, tc.v2, got, tc.want)
		}
	}
}

func TestRemindMeLaterReappearsOnNextLaunchIffApplicable(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(githubRelease{
			TagName: "v1.0.15",
			Assets: []githubAsset{
				{
					Name:               "nixlabs-discord-helper_1.0.15_amd64.deb",
					BrowserDownloadURL: "http://" + r.Host + "/download.deb",
					Size:               1024,
				},
			},
		})
	}))
	defer srv.Close()

	// First launch on 1.0.14 -> update available
	m1 := NewManagerWithConfig("1.0.14", srv.URL, "", t.TempDir(), srv.Client())
	st, err := m1.CheckForUpdate(context.Background())
	if err != nil {
		t.Fatalf("CheckForUpdate failed: %v", err)
	}
	if !st.Available || st.LatestVersion != "1.0.15" || st.Dismissed {
		t.Fatalf("unexpected initial status: %+v", st)
	}

	// Remind me later -> dismissed for this launch session
	st = m1.Dismiss()
	if !st.Dismissed {
		t.Fatalf("expected Dismissed=true, got %+v", st)
	}
	st2, _ := m1.CheckForUpdate(context.Background())
	if !st2.Dismissed {
		t.Fatalf("expected cached check to stay dismissed in same session")
	}

	// Next launch still on 1.0.14 -> modal reappears
	m2 := NewManagerWithConfig("1.0.14", srv.URL, "", t.TempDir(), srv.Client())
	st3, err := m2.CheckForUpdate(context.Background())
	if err != nil {
		t.Fatalf("next-launch CheckForUpdate failed: %v", err)
	}
	if !st3.Available || st3.Dismissed {
		t.Fatalf("expected modal to reappear on next launch, got %+v", st3)
	}

	// Next launch already on 1.0.15 -> not applicable, modal does not appear
	m3 := NewManagerWithConfig("1.0.15", srv.URL, "", t.TempDir(), srv.Client())
	st4, err := m3.CheckForUpdate(context.Background())
	if err != nil {
		t.Fatalf("up-to-date CheckForUpdate failed: %v", err)
	}
	if st4.Available || st4.Phase != PhaseIdle {
		t.Fatalf("expected no update available when already on latest, got %+v", st4)
	}
}

func buildSyntheticDeb(t *testing.T, binBytes []byte) []byte {
	t.Helper()

	var tarBuf bytes.Buffer
	gz := gzip.NewWriter(&tarBuf)
	tw := tar.NewWriter(gz)
	hdr := &tar.Header{
		Name: "./usr/lib/nixlabs-discord-helper/nixlabs-discord-helper-bin",
		Mode: 0755,
		Size: int64(len(binBytes)),
	}
	if err := tw.WriteHeader(hdr); err != nil {
		t.Fatal(err)
	}
	if _, err := tw.Write(binBytes); err != nil {
		t.Fatal(err)
	}
	if err := tw.Close(); err != nil {
		t.Fatal(err)
	}
	if err := gz.Close(); err != nil {
		t.Fatal(err)
	}

	var ar bytes.Buffer
	ar.WriteString("!<arch>\n")
	writeArMember(&ar, "debian-binary", []byte("2.0\n"))
	writeArMember(&ar, "data.tar.gz", tarBuf.Bytes())
	return ar.Bytes()
}

func writeArMember(buf *bytes.Buffer, name string, data []byte) {
	header := fmt.Sprintf("%-16s%-12d%-6d%-6d%-8o%-10d`\n", name, 0, 0, 0, 0100644, len(data))
	buf.WriteString(header)
	buf.Write(data)
	if len(data)%2 != 0 {
		buf.WriteByte('\n')
	}
}

func TestStartUpdateDownloadsAndInstallsDebAtomically(t *testing.T) {
	newBinary := append([]byte("\x7fELF"), bytes.Repeat([]byte("NIXLABS-NEW-BIN-"), 8192)...)
	debBytes := buildSyntheticDeb(t, newBinary)

	var serverURL string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/release" {
			_ = json.NewEncoder(w).Encode(githubRelease{
				TagName: "v1.0.15",
				Assets: []githubAsset{
					{
						Name:               "nixlabs-discord-helper_1.0.15_amd64.deb",
						BrowserDownloadURL: serverURL + "/package.deb",
						Size:               int64(len(debBytes)),
					},
				},
			})
			return
		}
		if r.URL.Path == "/package.deb" {
			w.Header().Set("Content-Length", fmt.Sprintf("%d", len(debBytes)))
			_, _ = w.Write(debBytes)
			return
		}
		http.NotFound(w, r)
	}))
	defer srv.Close()
	serverURL = srv.URL

	tmpDir := t.TempDir()
	targetBin := filepath.Join(tmpDir, "nixlabs-discord-helper-bin")
	if err := os.WriteFile(targetBin, []byte("\x7fELF-old"), 0755); err != nil {
		t.Fatal(err)
	}

	mgr := NewManagerWithConfig("1.0.14", srv.URL+"/release", targetBin, tmpDir, srv.Client())
	ch := mgr.Subscribe()
	defer mgr.Unsubscribe(ch)

	if _, err := mgr.CheckForUpdate(context.Background()); err != nil {
		t.Fatalf("CheckForUpdate failed: %v", err)
	}
	if _, err := mgr.StartUpdate(context.Background()); err != nil {
		t.Fatalf("StartUpdate failed: %v", err)
	}

	var seenDownloading, seenInstalling, seenCompleted bool
	deadline := time.After(5 * time.Second)
	for !seenCompleted {
		select {
		case ev := <-ch:
			switch ev.Phase {
			case PhaseDownloading:
				seenDownloading = true
				if !strings.HasPrefix(ev.Subtitle, "Downloading ") || !strings.HasSuffix(ev.Subtitle, "%") {
					t.Fatalf("unexpected download subtitle: %q", ev.Subtitle)
				}
				if ev.Percent > 80 {
					t.Fatalf("download percent should not exceed 80%% before install, got %d", ev.Percent)
				}
			case PhaseInstalling:
				seenInstalling = true
				if ev.Subtitle != "Installing..." {
					t.Fatalf("unexpected install subtitle: %q", ev.Subtitle)
				}
				if ev.Percent < 80 || ev.Percent >= 100 {
					t.Fatalf("expected install percent in [80,100), got %d", ev.Percent)
				}
			case PhaseCompleted:
				seenCompleted = true
				if ev.Percent != 100 || ev.DownloadPercent != 100 {
					t.Fatalf("expected 100%% at end of installation, got percent=%d download=%d", ev.Percent, ev.DownloadPercent)
				}
				if ev.Subtitle != "Installing..." {
					t.Fatalf("expected subtitle 'Installing...' at end, got %q", ev.Subtitle)
				}
			case PhaseError:
				t.Fatalf("update failed unexpectedly: %s", ev.Error)
			}
		case <-deadline:
			t.Fatalf("timed out waiting for update completion (last status: %+v)", mgr.CurrentStatus())
		}
	}

	if !seenDownloading || !seenInstalling {
		t.Fatalf("expected both downloading (%v) and installing (%v) phases", seenDownloading, seenInstalling)
	}

	gotBytes, err := os.ReadFile(targetBin)
	if err != nil {
		t.Fatalf("failed reading replaced binary: %v", err)
	}
	if !bytes.Equal(gotBytes, newBinary) {
		t.Fatalf("replaced binary content mismatch (got %d bytes, want %d bytes)", len(gotBytes), len(newBinary))
	}
	info, err := os.Stat(targetBin)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm()&0111 == 0 {
		t.Fatalf("replaced binary is not executable: mode %v", info.Mode().Perm())
	}
}
