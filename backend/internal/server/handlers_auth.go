package server

import (
	"encoding/json"
	"net/http"
	"os/exec"
	"strings"
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
	s.questEngine.ClearToken()
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
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'")
	_, _ = w.Write([]byte(`<!doctype html><html><head><meta charset="utf-8"><title>Nixlabs account created</title><style>body{font-family:sans-serif;max-width:34rem;margin:5rem auto;padding:1rem}</style></head><body><h1>Account created</h1><p>Return to Discord Helper and sign in with your new Nixlabs account.</p></body></html>`))
}
