package auth

import (
	"io"
	"net/http"
	"strings"
	"testing"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func TestLoginPersistsVerifiedSessionAcrossRestarts(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	profileRequests := 0
	m := &Manager{client: &http.Client{Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
		header := make(http.Header)
		body := `{"success":true,"redirect":"https://nixlabs.tech"}`
		switch req.URL.Path {
		case "/api/login":
			header.Add("Set-Cookie", "_nixlabs_session=signed-session; Domain=.nixlabs.tech; Path=/; HttpOnly; Secure; SameSite=Lax")
		case "/api/profile":
			profileRequests++
			if req.Header.Get("Cookie") != "_nixlabs_session=signed-session" {
				t.Fatal("new login cookie was not sent to the profile endpoint")
			}
			body = `{"user":{"id":"test","name":"Test","handle":"test"},"account":{"banned":false}}`
		default:
			t.Fatalf("unexpected request: %s", req.URL.Path)
		}
		return &http.Response{StatusCode: http.StatusOK, Header: header, Body: io.NopCloser(strings.NewReader(body))}, nil
	})}}
	user, _, err := m.LoginInApp("test", "correct-password")
	if err != nil || user["id"] != "test" {
		t.Fatalf("login did not return the verified user: %v", err)
	}
	// Reload from disk exactly as a newly opened desktop window does.
	restarted := NewManager()
	restarted.client = m.client
	if ok, user, _ := restarted.GetSession(); !ok || user["id"] != "test" {
		t.Fatal("successful login was not persisted")
	}
	if profileRequests != 1 {
		t.Fatalf("profile fetched %d times, want one verification", profileRequests)
	}
}

func TestLoginRejectsIncompleteSessions(t *testing.T) {
	for _, tc := range []struct {
		name          string
		loginStatus   int
		loginBody     string
		cookie        string
		profileStatus int
		profileBody   string
		wantError     string
	}{
		{name: "wrong password", loginStatus: 401, loginBody: `{"error":"Invalid username or password."}`, wantError: "Invalid username or password"},
		{name: "no cookie", loginBody: `{"success":true}`, wantError: "did not establish a session"},
		{name: "migration", loginBody: `{"success":true,"migrationRequired":true,"token":"migration-only"}`, wantError: "account setup"},
		{name: "invalid login response", loginBody: `not json`, wantError: "invalid login response"},
		{name: "unsuccessful login", loginBody: `{"success":false}`, wantError: "authentication failed"},
		{name: "expired cookie", loginBody: `{"success":true}`, cookie: "session", profileStatus: 401, wantError: "HTTP 401"},
		{name: "profile unavailable", loginBody: `{"success":true}`, cookie: "session", profileStatus: 503, wantError: "HTTP 503"},
		{name: "malformed profile", loginBody: `{"success":true}`, cookie: "session", profileBody: `not json`, wantError: "invalid profile"},
		{name: "missing user", loginBody: `{"success":true}`, cookie: "session", profileBody: `{"success":true}`, wantError: "did not return a user"},
		{name: "empty user", loginBody: `{"success":true}`, cookie: "session", profileBody: `{"user":{}}`, wantError: "did not return a user"},
		{name: "banned account", loginBody: `{"success":true}`, cookie: "session", profileBody: `{"user":{"id":"test"},"account":{"banned":true}}`, wantError: "suspended or banned"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			t.Setenv("XDG_CONFIG_HOME", t.TempDir())
			m := &Manager{client: &http.Client{Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
				status, body := tc.loginStatus, tc.loginBody
				header := make(http.Header)
				if req.URL.Path == "/api/profile" {
					status, body = tc.profileStatus, tc.profileBody
				} else if tc.cookie != "" {
					header.Set("Set-Cookie", "_nixlabs_session="+tc.cookie)
				}
				if status == 0 {
					status = http.StatusOK
				}
				return &http.Response{StatusCode: status, Header: header, Body: io.NopCloser(strings.NewReader(body))}, nil
			})}}
			if _, _, err := m.LoginInApp("test", "password"); err == nil || !strings.Contains(err.Error(), tc.wantError) {
				t.Fatalf("got %v, want error containing %q", err, tc.wantError)
			}
			if m.auth != nil || NewManager().auth != nil {
				t.Fatal("failed login saved an unusable session")
			}
		})
	}
}

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
