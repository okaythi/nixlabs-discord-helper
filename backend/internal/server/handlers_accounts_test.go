package server

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"testing/fstest"
	"time"

	"nixlabs-discord-helper/internal/auth"
	"nixlabs-discord-helper/internal/discord"
	"nixlabs-discord-helper/internal/quests"
)

func TestDesktopLoginSessionAndAccountRequests(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	originalTransport := http.DefaultTransport
	logoutDone := make(chan struct{})
	http.DefaultTransport = questRoundTripper(func(req *http.Request) (*http.Response, error) {
		header := make(http.Header)
		status, body := http.StatusOK, `{"success":true}`
		switch req.URL.Path {
		case "/api/login":
			var credentials struct{ Identifier, Password string }
			if err := json.NewDecoder(req.Body).Decode(&credentials); err != nil {
				t.Error(err)
			}
			if credentials.Password != "correct" {
				status, body = http.StatusUnauthorized, `{"error":"Invalid username or password."}`
			} else {
				header.Set("Set-Cookie", "_nixlabs_session=verified-session; Domain=.nixlabs.tech; HttpOnly; Secure; SameSite=Lax")
			}
		case "/api/profile":
			if req.Header.Get("Cookie") != "_nixlabs_session=verified-session" {
				t.Error("profile request lost the login cookie")
			}
			body = `{"user":{"id":"test","name":"Test","handle":"test"},"account":{"banned":false}}`
		case "/api/third-party-auth/discord-helper/bot-profile", "/api/third-party-auth/discord-helper/token", "/api/third-party-auth/discord-helper/snowflakes/123":
			if req.Header.Get("Cookie") != "_nixlabs_session=verified-session" || req.Header.Get("Authorization") != "Bearer verified-session" || req.Header.Get("Origin") != "" {
				t.Error("account request did not use the backend session")
			}
			if req.URL.Path == "/api/third-party-auth/discord-helper/bot-profile" && req.URL.RawQuery != "refresh=true" {
				t.Error("account refresh query was lost")
			}
			header.Set("Set-Cookie", "must-not-reach-frontend=secret")
		case "/api/logout":
			close(logoutDone)
		case "/app":
			status, body = http.StatusNotFound, ""
		default:
			t.Errorf("unexpected upstream endpoint: %s", req.URL.Path)
		}
		return &http.Response{StatusCode: status, Header: header, Body: io.NopCloser(strings.NewReader(body))}, nil
	})
	t.Cleanup(func() { http.DefaultTransport = originalTransport })
	server := NewServer(51234, auth.NewManager(), quests.NewEngine(discord.NewClient()), fstest.MapFS{})
	routes := server.Routes()
	request := func(method, path, body string) *httptest.ResponseRecorder {
		t.Helper()
		req := httptest.NewRequest(method, "http://127.0.0.1:51234"+path, strings.NewReader(body))
		req.Header.Set("Origin", "http://127.0.0.1:51234")
		req.Header.Set("Sec-Fetch-Site", "same-origin")
		rec := httptest.NewRecorder()
		routes.ServeHTTP(rec, req)
		if rec.Header().Get("Set-Cookie") != "" || strings.Contains(rec.Body.String(), "verified-session") {
			t.Fatal("backend session leaked to the frontend")
		}
		return rec
	}
	if res := request(http.MethodPost, "/api/accounts/login", `{"identifier":"test","password":"wrong"}`); res.Code != 401 || !strings.Contains(res.Body.String(), "Invalid username or password") {
		t.Fatalf("wrong password returned %d: %s", res.Code, res.Body.String())
	}
	if res := request(http.MethodPost, "/api/accounts/login", `{"identifier":"test","password":"correct"}`); res.Code != 200 || !strings.Contains(res.Body.String(), `"success":true`) {
		t.Fatalf("correct password returned %d: %s", res.Code, res.Body.String())
	}
	if res := request(http.MethodGet, "/api/accounts/session", ""); res.Code != 200 || !strings.Contains(res.Body.String(), `"authenticated":true`) || !strings.Contains(res.Body.String(), `"handle":"test"`) {
		t.Fatalf("session returned %d: %s", res.Code, res.Body.String())
	}
	for _, tc := range []struct{ method, path string }{
		{http.MethodGet, "/api/accounts/third-party-auth/discord-helper/bot-profile?refresh=true"},
		{http.MethodPut, "/api/accounts/third-party-auth/discord-helper/token"},
		{http.MethodDelete, "/api/accounts/third-party-auth/discord-helper/token"},
		{http.MethodGet, "/api/accounts/third-party-auth/discord-helper/snowflakes/123"},
	} {
		if res := request(tc.method, tc.path, "{}"); res.Code != 200 {
			t.Fatalf("account request returned %d: %s", res.Code, res.Body.String())
		}
	}
	if res := request(http.MethodPost, "/api/accounts/third-party-auth/discord-helper/reveal", ""); res.Code != 404 {
		t.Fatal("credential reveal was exposed to the frontend")
	}
	if res := request(http.MethodPost, "/api/accounts/logout", ""); res.Code != 200 {
		t.Fatalf("logout returned %d", res.Code)
	}
	select {
	case <-logoutDone:
	case <-time.After(time.Second):
		t.Fatal("logout did not revoke the Accounts session")
	}
	if res := request(http.MethodGet, "/api/accounts/session", ""); strings.Contains(res.Body.String(), `"authenticated":true`) {
		t.Fatal("logout retained the session")
	}
	if res := request(http.MethodGet, "/api/accounts/third-party-auth/discord-helper/bot-profile", ""); res.Code != http.StatusUnauthorized {
		t.Fatal("account requests worked after logout")
	}
}
