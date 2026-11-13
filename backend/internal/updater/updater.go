package updater

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"nixlabs-discord-helper/internal/config"
)

type Phase string

const (
	PhaseIdle        Phase = "idle"
	PhaseAvailable   Phase = "available"
	PhaseDownloading Phase = "downloading"
	PhaseInstalling  Phase = "installing"
	PhaseCompleted   Phase = "completed"
	PhaseError       Phase = "error"
)

type Status struct {
	Available       bool   `json:"available"`
	CurrentVersion  string `json:"current_version"`
	LatestVersion   string `json:"latest_version,omitempty"`
	Phase           Phase  `json:"phase"`
	Percent         int    `json:"percent"`
	DownloadPercent int    `json:"download_percent"`
	Subtitle        string `json:"subtitle"`
	Dismissed       bool   `json:"dismissed"`
	StorageType     string `json:"storage_type,omitempty"`
	Error           string `json:"error,omitempty"`
}

type githubAsset struct {
	Name               string `json:"name"`
	BrowserDownloadURL string `json:"browser_download_url"`
	Size               int64  `json:"size"`
}

type githubRelease struct {
	TagName string        `json:"tag_name"`
	Name    string        `json:"name"`
	Assets  []githubAsset `json:"assets"`
}

type Manager struct {
	mu          sync.RWMutex
	status      Status
	checked     bool
	assetURL    string
	assetName   string
	assetSize   int64
	releaseURL  string
	targetPath  string
	dataDir     string
	client      *http.Client
	subscribers map[chan Status]struct{}
}

func NewManager() *Manager {
	releaseURL := os.Getenv("NIXLABS_UPDATE_URL")
	if releaseURL == "" {
		releaseURL = config.GitHubLatestReleaseURL
	}
	return NewManagerWithConfig(config.Version, releaseURL, "", "", nil)
}

func NewManagerWithConfig(currentVersion, releaseURL, targetPath, dataDir string, client *http.Client) *Manager {
	if currentVersion == "" {
		currentVersion = config.Version
	}
	if releaseURL == "" {
		releaseURL = config.GitHubLatestReleaseURL
	}
	if client == nil {
		client = &http.Client{Timeout: 2 * time.Minute}
	}
	return &Manager{
		status: Status{
			CurrentVersion: strings.TrimPrefix(strings.TrimPrefix(currentVersion, "v"), "V"),
			Phase:          PhaseIdle,
		},
		releaseURL:  releaseURL,
		targetPath:  targetPath,
		dataDir:     dataDir,
		client:      client,
		subscribers: make(map[chan Status]struct{}),
	}
}

func (m *Manager) CurrentStatus() Status {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.status
}

func (m *Manager) Subscribe() chan Status {
	ch := make(chan Status, 32)
	m.mu.Lock()
	m.subscribers[ch] = struct{}{}
	current := m.status
	m.mu.Unlock()
	ch <- current
	return ch
}

func (m *Manager) Unsubscribe(ch chan Status) {
	m.mu.Lock()
	if _, ok := m.subscribers[ch]; ok {
		delete(m.subscribers, ch)
		close(ch)
	}
	m.mu.Unlock()
}

func (m *Manager) broadcastLocked() {
	st := m.status
	for ch := range m.subscribers {
		select {
		case ch <- st:
		default:
			// Drop oldest buffered event if channel is full so slow clients never block writes.
			select {
			case <-ch:
			default:
			}
			select {
			case ch <- st:
			default:
			}
		}
	}
}

// CompareVersions compares two semver-like strings (e.g. "v1.0.14" and "1.0.15").
// Returns 1 if v1 > v2, -1 if v1 < v2, and 0 if equal.
func CompareVersions(v1, v2 string) int {
	p1 := parseVersionParts(v1)
	p2 := parseVersionParts(v2)
	maxLen := len(p1)
	if len(p2) > maxLen {
		maxLen = len(p2)
	}
	for i := 0; i < maxLen; i++ {
		var a, b int
		if i < len(p1) {
			a = p1[i]
		}
		if i < len(p2) {
			b = p2[i]
		}
		if a > b {
			return 1
		}
		if a < b {
			return -1
		}
	}
	return 0
}

func parseVersionParts(v string) []int {
	v = strings.TrimSpace(v)
	v = strings.TrimPrefix(strings.TrimPrefix(v, "v"), "V")
	if idx := strings.IndexAny(v, "-+"); idx >= 0 {
		v = v[:idx]
	}
	if v == "" {
		return nil
	}
	segments := strings.Split(v, ".")
	parts := make([]int, len(segments))
	for i, seg := range segments {
		n, err := strconv.Atoi(strings.TrimSpace(seg))
		if err == nil {
			parts[i] = n
		}
	}
	return parts
}

// DetectStorageType inspects Linux sysfs rotational flag for the block device
// backing dir, returning "hdd" for rotational media or "ssd" otherwise.
func DetectStorageType(dir string) string {
	info, err := os.Stat(dir)
	if err != nil {
		return "ssd"
	}
	stat, ok := info.Sys().(*syscall.Stat_t)
	if !ok {
		return "ssd"
	}
	dev := uint64(stat.Dev)
	major := ((dev >> 8) & 0xfff) | ((dev >> 32) & ^uint64(0xfff))
	minor := (dev & 0xff) | ((dev >> 12) & 0xffffff00)

	candidates := []string{
		fmt.Sprintf("/sys/dev/block/%d:%d/queue/rotational", major, minor),
		fmt.Sprintf("/sys/dev/block/%d:%d/../queue/rotational", major, minor),
	}
	for _, p := range candidates {
		data, err := os.ReadFile(p)
		if err == nil {
			val := strings.TrimSpace(string(data))
			if val == "1" {
				return "hdd"
			}
			if val == "0" {
				return "ssd"
			}
		}
	}
	return "ssd"
}

func (m *Manager) CheckForUpdate(ctx context.Context, force ...bool) (Status, error) {
	forceRefresh := len(force) > 0 && force[0]

	m.mu.RLock()
	if m.checked && !forceRefresh {
		st := m.status
		m.mu.RUnlock()
		return st, nil
	}
	if m.status.Phase == PhaseDownloading || m.status.Phase == PhaseInstalling || m.status.Phase == PhaseCompleted {
		st := m.status
		m.mu.RUnlock()
		return st, nil
	}
	currentVer := m.status.CurrentVersion
	releaseURL := m.releaseURL
	m.mu.RUnlock()

	checkCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()

	req, err := http.NewRequestWithContext(checkCtx, http.MethodGet, releaseURL, nil)
	if err != nil {
		return m.CurrentStatus(), err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("User-Agent", "nixlabs-discord-helper/"+currentVer)

	resp, err := m.client.Do(req)
	if err != nil {
		return m.CurrentStatus(), err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return m.CurrentStatus(), fmt.Errorf("upstream release check failed: HTTP %d", resp.StatusCode)
	}

	var rel githubRelease
	if err := json.NewDecoder(io.LimitReader(resp.Body, 512*1024)).Decode(&rel); err != nil {
		return m.CurrentStatus(), fmt.Errorf("invalid release metadata: %w", err)
	}

	latestVer := strings.TrimPrefix(strings.TrimPrefix(strings.TrimSpace(rel.TagName), "v"), "V")
	assetURL, assetName, assetSize := selectReleaseAsset(rel.Assets)

	m.mu.Lock()
	defer m.mu.Unlock()
	m.checked = true

	if m.status.Phase == PhaseDownloading || m.status.Phase == PhaseInstalling || m.status.Phase == PhaseCompleted {
		return m.status, nil
	}

	if latestVer != "" && CompareVersions(latestVer, m.status.CurrentVersion) > 0 && assetURL != "" {
		m.assetURL = assetURL
		m.assetName = assetName
		m.assetSize = assetSize
		m.status.Available = true
		m.status.LatestVersion = latestVer
		m.status.Phase = PhaseAvailable
		m.status.Error = ""
	} else {
		m.status.Available = false
		m.status.LatestVersion = latestVer
		m.status.Phase = PhaseIdle
	}
	m.broadcastLocked()
	return m.status, nil
}

func selectReleaseAsset(assets []githubAsset) (string, string, int64) {
	var fallback githubAsset
	for _, a := range assets {
		if a.BrowserDownloadURL == "" {
			continue
		}
		lower := strings.ToLower(a.Name)
		if strings.HasSuffix(lower, "_amd64.deb") || strings.HasSuffix(lower, ".deb") {
			return a.BrowserDownloadURL, a.Name, a.Size
		}
		if strings.Contains(lower, "nixlabs-discord-helper") && !strings.Contains(lower, ".sig") && !strings.Contains(lower, ".sha") {
			fallback = a
		}
	}
	return fallback.BrowserDownloadURL, fallback.Name, fallback.Size
}

func (m *Manager) Dismiss() Status {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.status.Dismissed = true
	m.broadcastLocked()
	return m.status
}

func (m *Manager) StartUpdate(ctx context.Context) (Status, error) {
	m.mu.Lock()
	if m.status.Phase == PhaseDownloading || m.status.Phase == PhaseInstalling {
		st := m.status
		m.mu.Unlock()
		return st, nil
	}
	if m.status.Phase == PhaseCompleted {
		st := m.status
		m.mu.Unlock()
		return st, nil
	}
	assetURL := m.assetURL
	m.mu.Unlock()

	if assetURL == "" {
		if _, err := m.CheckForUpdate(ctx, true); err != nil {
			return m.CurrentStatus(), err
		}
		m.mu.RLock()
		assetURL = m.assetURL
		m.mu.RUnlock()
		if assetURL == "" {
			return m.CurrentStatus(), errors.New("no update available")
		}
	}

	m.mu.Lock()
	if m.status.Phase == PhaseDownloading || m.status.Phase == PhaseInstalling {
		st := m.status
		m.mu.Unlock()
		return st, nil
	}
	targetDir := m.resolveBaseTargetDir()
	storageType := DetectStorageType(targetDir)
	m.status.Phase = PhaseDownloading
	m.status.Percent = 0
	m.status.DownloadPercent = 0
	m.status.Subtitle = "Downloading 0%"
	m.status.StorageType = storageType
	m.status.Error = ""
	m.status.Dismissed = false
	m.broadcastLocked()
	st := m.status
	expectedSize := m.assetSize
	m.mu.Unlock()

	go m.runUpdate(assetURL, expectedSize, storageType)

	return st, nil
}

func (m *Manager) resolveBaseTargetDir() string {
	if m.targetPath != "" {
		return filepath.Dir(m.targetPath)
	}
	execPath, err := os.Executable()
	if err != nil {
		return m.effectiveDataDir()
	}
	resolved, err := filepath.EvalSymlinks(execPath)
	if err == nil {
		execPath = resolved
	}
	return filepath.Dir(execPath)
}

func (m *Manager) effectiveDataDir() string {
	if m.dataDir != "" {
		_ = os.MkdirAll(m.dataDir, 0700)
		return m.dataDir
	}
	return config.GetDataDir()
}

func (m *Manager) setError(err error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.status.Phase = PhaseError
	m.status.Error = err.Error()
	m.broadcastLocked()
}

func (m *Manager) runUpdate(assetURL string, expectedSize int64, storageType string) {
	payload, err := m.downloadInMemory(assetURL, expectedSize)
	if err != nil {
		m.setError(err)
		return
	}

	m.mu.Lock()
	m.status.Phase = PhaseInstalling
	m.status.DownloadPercent = 100
	m.status.Percent = 82
	m.status.Subtitle = "Installing..."
	m.broadcastLocked()
	m.mu.Unlock()

	binData, launcherData, err := extractPayload(payload)
	if err != nil {
		m.setError(err)
		return
	}

	m.mu.Lock()
	m.status.Percent = 88
	m.status.Subtitle = "Installing..."
	m.broadcastLocked()
	m.mu.Unlock()

	if err := m.installBinary(binData, launcherData, storageType); err != nil {
		m.setError(err)
		return
	}

	m.mu.Lock()
	m.status.Phase = PhaseCompleted
	m.status.DownloadPercent = 100
	m.status.Percent = 100
	m.status.Subtitle = "Installing..."
	if m.status.LatestVersion != "" {
		m.status.CurrentVersion = m.status.LatestVersion
	}
	m.broadcastLocked()
	m.mu.Unlock()
}

func (m *Manager) downloadInMemory(assetURL string, expectedSize int64) ([]byte, error) {
	req, err := http.NewRequest(http.MethodGet, assetURL, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "nixlabs-discord-helper/"+m.CurrentStatus().CurrentVersion)

	resp, err := m.client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("download failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("download failed: HTTP %d", resp.StatusCode)
	}

	totalSize := resp.ContentLength
	if totalSize <= 0 && expectedSize > 0 {
		totalSize = expectedSize
	}

	var buf bytes.Buffer
	if totalSize > 0 && totalSize < 256*1024*1024 {
		buf.Grow(int(totalSize))
	}

	readBuf := make([]byte, 128*1024)
	var downloaded int64
	lastDownloadPct := -1

	for {
		n, readErr := resp.Body.Read(readBuf)
		if n > 0 {
			buf.Write(readBuf[:n])
			downloaded += int64(n)
			if totalSize > 0 {
				dlPct := int((downloaded * 100) / totalSize)
				if dlPct > 100 {
					dlPct = 100
				}
				if dlPct != lastDownloadPct {
					lastDownloadPct = dlPct
					m.mu.Lock()
					m.status.Phase = PhaseDownloading
					m.status.DownloadPercent = dlPct
					m.status.Percent = (dlPct * 80) / 100
					m.status.Subtitle = fmt.Sprintf("Downloading %d%%", dlPct)
					m.broadcastLocked()
					m.mu.Unlock()
				}
			}
		}
		if readErr == io.EOF {
			break
		}
		if readErr != nil {
			return nil, fmt.Errorf("download interrupted: %w", readErr)
		}
	}

	if lastDownloadPct < 100 {
		m.mu.Lock()
		m.status.Phase = PhaseDownloading
		m.status.DownloadPercent = 100
		m.status.Percent = 80
		m.status.Subtitle = "Downloading 100%"
		m.broadcastLocked()
		m.mu.Unlock()
	}

	return buf.Bytes(), nil
}

func extractPayload(payload []byte) (binData []byte, launcherData []byte, err error) {
	if bytes.HasPrefix(payload, []byte("!<arch>\n")) {
		return extractFromDebInMemory(payload)
	}
	if bytes.HasPrefix(payload, []byte("\x7fELF")) {
		return payload, nil, nil
	}
	return nil, nil, errors.New("unsupported update package format")
}

func extractFromDebInMemory(debBytes []byte) ([]byte, []byte, error) {
	const arMagic = "!<arch>\n"
	if !bytes.HasPrefix(debBytes, []byte(arMagic)) {
		return nil, nil, errors.New("invalid deb archive header")
	}

	offset := len(arMagic)
	var dataMemberName string
	var dataMemberBytes []byte

	for offset+60 <= len(debBytes) {
		hdr := debBytes[offset : offset+60]
		if hdr[58] != '`' || hdr[59] != '\n' {
			return nil, nil, errors.New("corrupt ar member header in deb package")
		}
		name := strings.TrimSpace(string(hdr[0:16]))
		name = strings.TrimSuffix(name, "/")
		sizeStr := strings.TrimSpace(string(hdr[48:58]))
		size, err := strconv.Atoi(sizeStr)
		if err != nil || size < 0 {
			return nil, nil, fmt.Errorf("invalid ar member size %q", sizeStr)
		}
		offset += 60
		if offset+size > len(debBytes) {
			return nil, nil, errors.New("truncated ar member in deb package")
		}
		memberData := debBytes[offset : offset+size]
		offset += size
		if size%2 != 0 && offset < len(debBytes) {
			offset++
		}

		if strings.HasPrefix(name, "data.tar") {
			dataMemberName = name
			dataMemberBytes = memberData
			break
		}
	}

	if dataMemberBytes == nil {
		return nil, nil, errors.New("data.tar not found in deb package")
	}

	tarStream, closer, err := openDataTarStream(dataMemberName, dataMemberBytes, debBytes)
	if err != nil {
		return nil, nil, err
	}
	defer closer()

	tr := tar.NewReader(tarStream)
	var binData, launcherData []byte
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, nil, fmt.Errorf("failed reading package tar stream: %w", err)
		}
		clean := strings.TrimPrefix(strings.TrimPrefix(hdr.Name, "./"), "/")
		switch clean {
		case "usr/lib/nixlabs-discord-helper/nixlabs-discord-helper-bin":
			binData, err = io.ReadAll(tr)
			if err != nil {
				return nil, nil, fmt.Errorf("failed extracting binary: %w", err)
			}
		case "usr/lib/nixlabs-discord-helper/launcher.js":
			launcherData, _ = io.ReadAll(tr)
		}
	}

	if len(binData) == 0 {
		return nil, nil, errors.New("updated binary missing from package")
	}
	return binData, launcherData, nil
}

func openDataTarStream(memberName string, memberBytes, fullDeb []byte) (io.Reader, func(), error) {
	switch memberName {
	case "data.tar":
		return bytes.NewReader(memberBytes), func() {}, nil
	case "data.tar.gz":
		gz, err := gzip.NewReader(bytes.NewReader(memberBytes))
		if err != nil {
			return nil, nil, err
		}
		return gz, func() { _ = gz.Close() }, nil
	case "data.tar.zst":
		if r, closer, err := decompressViaPipe("zstd", []string{"-d", "-c"}, memberBytes); err == nil {
			return r, closer, nil
		}
		return decompressViaPipe("dpkg-deb", []string{"--fsys-tarfile", "/dev/stdin"}, fullDeb)
	case "data.tar.xz":
		if r, closer, err := decompressViaPipe("xz", []string{"-d", "-c"}, memberBytes); err == nil {
			return r, closer, nil
		}
		return decompressViaPipe("dpkg-deb", []string{"--fsys-tarfile", "/dev/stdin"}, fullDeb)
	default:
		return decompressViaPipe("dpkg-deb", []string{"--fsys-tarfile", "/dev/stdin"}, fullDeb)
	}
}

func decompressViaPipe(cmdName string, args []string, input []byte) (io.Reader, func(), error) {
	bin, err := exec.LookPath(cmdName)
	if err != nil {
		return nil, nil, err
	}
	cmd := exec.Command(bin, args...)
	cmd.Stdin = bytes.NewReader(input)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	out, err := cmd.Output()
	if err != nil {
		return nil, nil, fmt.Errorf("%s failed: %w (%s)", cmdName, err, strings.TrimSpace(stderr.String()))
	}
	return bytes.NewReader(out), func() {}, nil
}

func (m *Manager) installBinary(binData, launcherData []byte, storageType string) error {
	targetPath := m.targetPath
	if targetPath == "" {
		execPath, err := os.Executable()
		if err != nil {
			return fmt.Errorf("cannot locate current executable: %w", err)
		}
		if resolved, err := filepath.EvalSymlinks(execPath); err == nil {
			execPath = resolved
		}
		targetPath = execPath
	}

	targetDir := filepath.Dir(targetPath)
	stagingPath := filepath.Join(targetDir, fmt.Sprintf(".nixlabs-discord-helper-bin.tmp.%d", os.Getpid()))
	f, err := os.OpenFile(stagingPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0755)
	if err != nil {
		if isPermissionError(err) && m.targetPath == "" {
			// Fallback to XDG user data dir when /usr/lib/nixlabs-discord-helper is read-only.
			dataDir := m.effectiveDataDir()
			fallbackTarget := filepath.Join(dataDir, "nixlabs-discord-helper-bin")
			fallbackStorage := DetectStorageType(dataDir)
			fallbackStaging := filepath.Join(dataDir, fmt.Sprintf(".nixlabs-discord-helper-bin.tmp.%d", os.Getpid()))
			ff, ferr := os.OpenFile(fallbackStaging, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0755)
			if ferr != nil {
				return fmt.Errorf("failed to stage fallback binary: %w", ferr)
			}
			if werr := m.writeBinaryOptimized(ff, fallbackStaging, fallbackTarget, binData, fallbackStorage); werr != nil {
				return werr
			}
			ver := m.CurrentStatus().LatestVersion
			if ver != "" {
				_ = os.WriteFile(filepath.Join(dataDir, "version"), []byte(ver), 0644)
			}
			return nil
		}
		return fmt.Errorf("failed to stage updated binary: %w", err)
	}

	if err := m.writeBinaryOptimized(f, stagingPath, targetPath, binData, storageType); err != nil {
		return err
	}

	if len(launcherData) > 0 && m.targetPath == "" {
		launcherTarget := filepath.Join(targetDir, "launcher.js")
		launcherStaging := filepath.Join(targetDir, fmt.Sprintf(".launcher.js.tmp.%d", os.Getpid()))
		if lf, lerr := os.OpenFile(launcherStaging, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0755); lerr == nil {
			if _, werr := lf.Write(launcherData); werr == nil {
				_ = syscall.Fdatasync(int(lf.Fd()))
				_ = lf.Chmod(0755)
				_ = lf.Close()
				_ = os.Rename(launcherStaging, launcherTarget)
			} else {
				_ = lf.Close()
				_ = os.Remove(launcherStaging)
			}
		}
	}

	// Clean up any stale user-local fallback binary when in-place replacement succeeds.
	if m.targetPath == "" {
		dataDir := m.effectiveDataDir()
		_ = os.Remove(filepath.Join(dataDir, "nixlabs-discord-helper-bin"))
		_ = os.Remove(filepath.Join(dataDir, "version"))
	}
	return nil
}

func isPermissionError(err error) bool {
	return errors.Is(err, os.ErrPermission) || errors.Is(err, syscall.EACCES) || errors.Is(err, syscall.EROFS)
}

func (m *Manager) writeBinaryOptimized(f *os.File, stagingPath, targetPath string, data []byte, storageType string) error {
	cleanup := func() {
		_ = f.Close()
		_ = os.Remove(stagingPath)
	}

	fd := int(f.Fd())
	totalLen := len(data)
	// Pre-allocate a single contiguous disk extent before writing to eliminate fragmentation.
	if totalLen > 0 {
		_ = syscall.Fallocate(fd, 0, 0, int64(totalLen))
	}

	// Tune write block size by device geometry:
	// - HDD: large 2 MB contiguous sequential blocks minimize actuator seeks.
	// - SSD: 512 KB flash page-aligned sequential writes keep NVMe/SATA queues saturated.
	chunkSize := 512 * 1024
	if storageType == "hdd" {
		chunkSize = 2 * 1024 * 1024
	}

	written := 0
	for written < totalLen {
		end := written + chunkSize
		if end > totalLen {
			end = totalLen
		}
		n, err := f.Write(data[written:end])
		if err != nil {
			cleanup()
			return fmt.Errorf("failed writing staged binary: %w", err)
		}
		written += n

		pct := 88 + (written*8)/totalLen
		if pct > 96 {
			pct = 96
		}
		m.mu.Lock()
		m.status.Percent = pct
		m.status.Subtitle = "Installing..."
		m.broadcastLocked()
		m.mu.Unlock()
	}

	// Flush data blocks without redundant metadata updates.
	if err := syscall.Fdatasync(fd); err != nil {
		// Fallback to full Sync if Fdatasync is unsupported.
		if serr := f.Sync(); serr != nil {
			cleanup()
			return fmt.Errorf("failed syncing staged binary: %w", serr)
		}
	}

	if err := f.Chmod(0755); err != nil {
		cleanup()
		return fmt.Errorf("failed setting permissions on binary: %w", err)
	}
	if err := f.Close(); err != nil {
		_ = os.Remove(stagingPath)
		return fmt.Errorf("failed closing staged binary: %w", err)
	}

	// Atomic rename(2) guarantees zero-downtime replacement even while the old binary is executing.
	if err := os.Rename(stagingPath, targetPath); err != nil {
		_ = os.Remove(stagingPath)
		return fmt.Errorf("failed replacing binary: %w", err)
	}
	return nil
}
