package server

import (
	"encoding/json"
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

func TestHandleQuestsPagination(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	originalTransport := http.DefaultTransport
	http.DefaultTransport = questRoundTripper(func(req *http.Request) (*http.Response, error) {
		if req.URL.Path == "/api/third-party-auth/discord-helper/reveal" {
			return &http.Response{StatusCode: http.StatusOK, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(`{"token":"test-token"}`))}, nil
		}
		if req.URL.Path == "/api/v9/quests/@me" {
			body := `[
				{"id":"1","config":{"starts_at":"2026-10-01T00:00:00Z","task_config":{"tasks":{"WATCH_VIDEO":{"target":5}}}}},
				{"id":"2","config":{"starts_at":"2026-10-05T00:00:00Z","task_config":{"tasks":{"WATCH_VIDEO":{"target":5}}}}},
				{"id":"3","config":{"starts_at":"2026-10-10T00:00:00Z","task_config":{"tasks":{"WATCH_VIDEO":{"target":5}}}}}
			]`
			return &http.Response{StatusCode: http.StatusOK, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(body))}, nil
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

	// 1. Without pagination params
	reqAll := httptest.NewRequest(http.MethodGet, "/api/quests", nil)
	recAll := httptest.NewRecorder()
	server.handleQuests(recAll, reqAll)
	if recAll.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", recAll.Code)
	}
	var resAll struct {
		Quests  []quests.QuestNormalized `json:"quests"`
		Total   int                      `json:"total"`
		HasMore bool                     `json:"has_more"`
	}
	if err := json.Unmarshal(recAll.Body.Bytes(), &resAll); err != nil {
		t.Fatal(err)
	}
	if resAll.Total != 3 || len(resAll.Quests) != 3 || resAll.HasMore {
		t.Fatalf("unexpected resAll: %+v", resAll)
	}
	// Verify sorted order (most recent first: id 3, then 2, then 1)
	if resAll.Quests[0].ID != "3" || resAll.Quests[1].ID != "2" || resAll.Quests[2].ID != "1" {
		t.Fatalf("expected quests ordered most recent to oldest: got %s, %s, %s", resAll.Quests[0].ID, resAll.Quests[1].ID, resAll.Quests[2].ID)
	}

	// 2. With limit=2, offset=0
	reqPage1 := httptest.NewRequest(http.MethodGet, "/api/quests?limit=2&offset=0", nil)
	recPage1 := httptest.NewRecorder()
	server.handleQuests(recPage1, reqPage1)
	var resPage1 struct {
		Quests  []quests.QuestNormalized `json:"quests"`
		Total   int                      `json:"total"`
		Limit   int                      `json:"limit"`
		Offset  int                      `json:"offset"`
		HasMore bool                     `json:"has_more"`
	}
	if err := json.Unmarshal(recPage1.Body.Bytes(), &resPage1); err != nil {
		t.Fatal(err)
	}
	if len(resPage1.Quests) != 2 || resPage1.Total != 3 || !resPage1.HasMore {
		t.Fatalf("unexpected page 1: %+v", resPage1)
	}
	if resPage1.Quests[0].ID != "3" || resPage1.Quests[1].ID != "2" {
		t.Fatalf("unexpected items in page 1: %s, %s", resPage1.Quests[0].ID, resPage1.Quests[1].ID)
	}

	// 3. With limit=2, offset=2
	reqPage2 := httptest.NewRequest(http.MethodGet, "/api/quests?limit=2&offset=2", nil)
	recPage2 := httptest.NewRecorder()
	server.handleQuests(recPage2, reqPage2)
	var resPage2 struct {
		Quests  []quests.QuestNormalized `json:"quests"`
		Total   int                      `json:"total"`
		HasMore bool                     `json:"has_more"`
	}
	if err := json.Unmarshal(recPage2.Body.Bytes(), &resPage2); err != nil {
		t.Fatal(err)
	}
	if len(resPage2.Quests) != 1 || resPage2.Total != 3 || resPage2.HasMore {
		t.Fatalf("unexpected page 2: %+v", resPage2)
	}
	if resPage2.Quests[0].ID != "1" {
		t.Fatalf("unexpected items in page 2: %s", resPage2.Quests[0].ID)
	}
}
