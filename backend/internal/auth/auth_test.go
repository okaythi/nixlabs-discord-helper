package auth

import (
	"io"
	"net/http"
	"strings"
	"testing"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func TestDiscordCredentialRequestsCarryVerifiedSession(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	const session = "signed-session"
	var credentialRequests int
	m := &Manager{
		auth: &StoredAuth{SessionCookie: session},
		client: &http.Client{Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
			body := `{"user":{"id":"test"},"account":{}}`
			switch req.URL.Path {
			case "/api/profile":
				if req.Header.Get("Cookie") != "_nixlabs_session="+session {
					t.Errorf("profile request lacks session cookie")
				}
			case "/api/third-party-auth/discord-helper/reveal":
				credentialRequests++
				body = `{"token":"test-token"}`
			case "/api/third-party-auth/discord-helper/bot-profile":
				credentialRequests++
				body = `{"id":"123456789012345678"}`
			default:
				t.Errorf("unexpected request: %s", req.URL.Path)
			}
			if req.URL.Path != "/api/profile" {
				if req.Header.Get("Cookie") != "_nixlabs_session="+session || req.Header.Get("Authorization") != "Bearer "+session {
					t.Errorf("credential request lacks session authentication")
				}
			}
			return &http.Response{StatusCode: http.StatusOK, Body: io.NopCloser(strings.NewReader(body)), Header: make(http.Header)}, nil
		})},
	}
	if token, err := m.GetDiscordToken(); err != nil || token != "test-token" {
		t.Fatalf("reveal failed: %v", err)
	}
	if profile, err := m.GetDiscordBotProfile(); err != nil || !strings.Contains(string(profile), "123456789012345678") {
		t.Fatalf("bot profile failed: %v", err)
	}
	if credentialRequests != 2 {
		t.Fatalf("got %d credential requests, want 2", credentialRequests)
	}
}
