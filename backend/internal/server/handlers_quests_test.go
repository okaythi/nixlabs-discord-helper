package server

import (
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"time"

	"nixlabs-discord-helper/internal/auth"
	"nixlabs-discord-helper/internal/discord"
	"testing"

	"nixlabs-discord-helper/internal/quests"
)

func TestCancelWorksWithoutAccountService(t *testing.T) {
	server := &Server{questEngine: quests.NewEngine(nil)}
	req := httptest.NewRequest(http.MethodPost, "/api/quests/cancel", nil)
	response := httptest.NewRecorder()
	server.handleQuestCancel(response, req)
	if response.Code != http.StatusOK {
		t.Fatalf("cancel returned %d: %s", response.Code, response.Body.String())
	}
}

// A Stop received while credential lookup is pending must prevent that start
// request from launching a quest after Stop has already returned.
func TestStopInvalidatesPendingQuestStart(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	originalTransport := http.DefaultTransport
	entered := make(chan struct{})
	release := make(chan struct{})
	http.DefaultTransport = questRoundTripper(func(req *http.Request) (*http.Response, error) {
		if req.URL.Path == "/api/third-party-auth/discord-helper/reveal" {
			close(entered)
			<-release
			return &http.Response{StatusCode: http.StatusOK, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(`{"token":"test-token"}`))}, nil
		}
		return &http.Response{StatusCode: http.StatusNotFound, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(""))}, nil
	})
	t.Cleanup(func() { http.DefaultTransport = originalTransport })

	manager := auth.NewManager()
	manager.Save(&auth.StoredAuth{
		SessionCookie: "test-session",
		User:          map[string]interface{}{"id": "test-user"},
		UpdatedAt:     time.Now().UTC().Format(time.RFC3339),
	})
	engine := quests.NewEngine(discord.NewClient())
	server := &Server{authMgr: manager, questEngine: engine}
	startResponse := httptest.NewRecorder()
	startDone := make(chan struct{})
	go func() {
		server.handleQuestCompleteAll(startResponse, httptest.NewRequest(http.MethodPost, "/api/quests/complete-all", nil))
		close(startDone)
	}()
	select {
	case <-entered:
	case <-time.After(time.Second):
		t.Fatal("start did not reach credential lookup")
	}
	cancelResponse := httptest.NewRecorder()
	server.handleQuestCancel(cancelResponse, httptest.NewRequest(http.MethodPost, "/api/quests/cancel", nil))
	close(release)
	select {
	case <-startDone:
	case <-time.After(time.Second):
		t.Fatal("start did not finish after Stop")
	}
	if cancelResponse.Code != http.StatusOK || startResponse.Code != http.StatusConflict {
		t.Fatalf("unexpected Stop/start results: %d / %d", cancelResponse.Code, startResponse.Code)
	}
	if engine.CurrentProgress().Running {
		t.Fatal("quest started after Stop")
	}
}

type questRoundTripper func(*http.Request) (*http.Response, error)

func (fn questRoundTripper) RoundTrip(req *http.Request) (*http.Response, error) {
	return fn(req)
}
