package server

import (
	"net/http"
	"net/http/httptest"
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
