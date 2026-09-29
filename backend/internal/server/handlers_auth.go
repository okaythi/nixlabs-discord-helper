package server

import (
	"encoding/json"
	"net/http"
	"os/exec"
	"strings"
	"time"

	"nixlabs-discord-helper/internal/auth"
)

func (s *Server) handleSession(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")

	authed, user, account := s.authMgr.GetSession()
	if !authed {
		_ = json.NewEncoder(w).Encode(map[string]interface{}{"authenticated": false})
		return
	}

	_ = json.NewEncoder(w).Encode(map[string]interface{}{
		"authenticated": true,
		"user":          user,
		"account":       account,
	})
}

func (s *Server) handleLogin(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if r.Method != http.MethodPost {
		http.Error(w, `{"error":"Method not allowed"}`, http.StatusMethodNotAllowed)
		return
	}

	var reqBody struct {
		Identifier string `json:"identifier"`
		Password   string `json:"password"`
	}
	if err := json.NewDecoder(r.Body).Decode(&reqBody); err != nil || reqBody.Identifier == "" || reqBody.Password == "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "Username and password are required"})
		return
	}

	user, account, err := s.authMgr.LoginInApp(reqBody.Identifier, reqBody.Password)
	if err != nil {
		w.WriteHeader(http.StatusUnauthorized)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}

	_ = json.NewEncoder(w).Encode(map[string]interface{}{
		"success": true,
		"user":    user,
		"account": account,
	})
}

func (s *Server) handleLogout(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	s.authMgr.Logout()
	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleRegisterURL(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	url := s.authMgr.CreateRegisterURL(s.port)
	_ = json.NewEncoder(w).Encode(map[string]interface{}{
		"success": true,
		"url":     url,
	})
}

func (s *Server) handleOpenBrowser(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if r.Method != http.MethodPost {
		http.Error(w, `{"error":"Method not allowed"}`, http.StatusMethodNotAllowed)
		return
	}

	var reqBody struct {
		URL string `json:"url"`
	}
	if err := json.NewDecoder(r.Body).Decode(&reqBody); err != nil || reqBody.URL == "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "URL is required"})
		return
	}

	// Strictly validate that URL is a nixlabs origin
	if !strings.HasPrefix(reqBody.URL, "https://accounts.nixlabs.tech") &&
		!strings.HasPrefix(reqBody.URL, "https://nixlabs.tech") {
		w.WriteHeader(http.StatusForbidden)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "Untrusted redirect URL"})
		return
	}

	go func(target string) {
		_ = exec.Command("xdg-open", target).Start()
	}(reqBody.URL)

	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleAuthCallbackPage(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	html := `<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>nixlabs | Discord Helper - Account Connected</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #1e1f20; color: #e8eaed; display: flex; align-items: center; justify-content: center; min-height: 100vh; margin: 0; }
    .card { background: #252628; border: 1px solid #42454a; border-radius: 16px; padding: 32px; max-width: 420px; text-align: center; }
    h2 { margin-top: 0; color: #a8c7fa; }
    p { color: #adb2b9; font-size: 14px; line-height: 1.5; }
    .status { margin-top: 16px; font-weight: 500; font-size: 14px; }
  </style>
</head>
<body>
  <div class="card">
    <h2>nixlabs | Discord Helper</h2>
    <p>Transferring authentication to your desktop app…</p>
    <div id="status" class="status">Connecting…</div>
  </div>
  <script>
    (async function() {
      const statusEl = document.getElementById('status');
      try {
        const res = await fetch('https://accounts.nixlabs.tech/api/session', { credentials: 'include' });
        const data = await res.json();
        if (data && data.authenticated && data.user) {
          await fetch('/api/auth/browser-session', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
          });
          statusEl.textContent = 'Account connected successfully! You may close this tab.';
          statusEl.style.color = '#8cd6a7';
          setTimeout(() => window.close(), 1800);
        } else {
          statusEl.textContent = 'Session not verified. Please log in directly inside the app.';
          statusEl.style.color = '#ffb4ab';
        }
      } catch (err) {
        statusEl.textContent = 'Handoff completed! You may return to the app.';
        statusEl.style.color = '#8cd6a7';
      }
    })();
  </script>
</body>
</html>`
	_, _ = w.Write([]byte(html))
}

func (s *Server) handleBrowserSession(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	var body struct {
		Authenticated bool                   `json:"authenticated"`
		User          map[string]interface{} `json:"user"`
		Account       map[string]interface{} `json:"account"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err == nil && body.Authenticated && body.User != nil {
		s.authMgr.Save(&auth.StoredAuth{
			User:      body.User,
			Account:   body.Account,
			UpdatedAt: time.Now().UTC().Format(time.RFC3339),
		})
	}
	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}
