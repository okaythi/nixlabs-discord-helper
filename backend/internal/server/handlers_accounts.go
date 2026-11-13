package server

import (
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"strings"

	"nixlabs-discord-helper/internal/auth"
)

// Only the account operations used by this app can use the saved session.
// Session cookies and credential-reveal responses never reach the frontend.
func (s *Server) handleAccountsDiscord(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	path := strings.TrimPrefix(r.URL.Path, "/api/accounts")
	const base = "/third-party-auth/discord-helper/"
	allowedMethod := false
	switch {
	case path == base+"bot-profile":
		allowedMethod = r.Method == http.MethodGet
	case path == base+"token":
		allowedMethod = r.Method == http.MethodPut || r.Method == http.MethodDelete
	case strings.HasPrefix(path, base+"snowflakes/") && snowflakeIDPattern.MatchString(strings.TrimPrefix(path, base+"snowflakes/")):
		allowedMethod = r.Method == http.MethodGet
	default:
		http.NotFound(w, r)
		return
	}
	if !allowedMethod {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	endpoint := "/api" + path
	if r.URL.RawQuery != "" {
		endpoint += "?" + r.URL.RawQuery
	}
	var body io.Reader
	if r.Method == http.MethodPut {
		body = http.MaxBytesReader(w, r.Body, 8192)
	}
	resp, err := s.authMgr.DiscordAccountRequest(r.Context(), r.Method, endpoint, body)
	if err != nil {
		status := http.StatusServiceUnavailable
		message := "Accounts service is unavailable. Please try again."
		if errors.Is(err, auth.ErrNotAuthenticated) {
			status, message = http.StatusUnauthorized, "nixlabs_session_invalid"
		}
		w.WriteHeader(status)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": message})
		return
	}
	defer resp.Body.Close()
	if path == base+"token" && resp.StatusCode == http.StatusOK {
		s.questEngine.ClearToken()
	}
	w.WriteHeader(resp.StatusCode)
	_, _ = io.Copy(w, io.LimitReader(resp.Body, 64*1024))
}
