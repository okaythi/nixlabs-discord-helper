package server

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"testing/fstest"

	"nixlabs-discord-helper/internal/auth"
	"nixlabs-discord-helper/internal/quests"
	"nixlabs-discord-helper/internal/updater"
)

func TestUpdateHandlersStatusAndDismiss(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{
			"tag_name": "v1.0.15",
			"assets": [{"name": "nixlabs-discord-helper_1.0.15_amd64.deb", "browser_download_url": "http://127.0.0.1/pkg.deb", "size": 100}]
		}`))
	}))
	defer upstream.Close()

	mgr := updater.NewManagerWithConfig("1.0.14", upstream.URL, "", t.TempDir(), upstream.Client())
	srv := NewServer(51234, auth.NewManager(), quests.NewEngine(nil), fstest.MapFS{}, mgr)
	routes := srv.Routes()

	doReq := func(method, path string) *httptest.ResponseRecorder {
		req := httptest.NewRequest(method, "http://127.0.0.1:51234"+path, nil)
		req.Header.Set("Origin", "http://127.0.0.1:51234")
		req.Header.Set("Sec-Fetch-Site", "same-origin")
		rec := httptest.NewRecorder()
		routes.ServeHTTP(rec, req)
		return rec
	}

	res := doReq(http.MethodGet, "/api/update/status")
	if res.Code != http.StatusOK {
		t.Fatalf("status returned %d: %s", res.Code, res.Body.String())
	}
	var st updater.Status
	if err := json.Unmarshal(res.Body.Bytes(), &st); err != nil {
		t.Fatal(err)
	}
	if !st.Available || st.LatestVersion != "1.0.15" || st.Dismissed {
		t.Fatalf("unexpected status: %+v", st)
	}

	dismissRes := doReq(http.MethodPost, "/api/update/dismiss")
	if dismissRes.Code != http.StatusOK {
		t.Fatalf("dismiss returned %d: %s", dismissRes.Code, dismissRes.Body.String())
	}
	if err := json.Unmarshal(dismissRes.Body.Bytes(), &st); err != nil {
		t.Fatal(err)
	}
	if !st.Dismissed {
		t.Fatalf("expected Dismissed=true after POST /api/update/dismiss, got %+v", st)
	}
}
