package server

import (
	"encoding/json"
	"errors"
	"net/http"
	"regexp"
	"strings"

	"nixlabs-discord-helper/internal/auth"
)

func (s *Server) handleDiscordUser(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	profile, err := s.authMgr.GetDiscordBotProfile(r.URL.Query().Get("refresh") == "true")
	if err != nil {
		status := http.StatusServiceUnavailable
		code := "discord_token_unavailable"
		switch {
		case errors.Is(err, auth.ErrNotAuthenticated):
			status, code = http.StatusUnauthorized, "nixlabs_session_invalid"
		case errors.Is(err, auth.ErrDiscordTokenMissing):
			status, code = http.StatusNotFound, "discord_token_missing"
		case errors.Is(err, auth.ErrDiscordTokenInvalid):
			status, code = http.StatusUnprocessableEntity, "discord_token_invalid"
		}
		w.WriteHeader(status)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": code})
		return
	}

	_, _ = w.Write(profile)
}

var snowflakeIDPattern = regexp.MustCompile(`^[0-9]{1,20}$`)

func (s *Server) handleDiscordSnowflake(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	id := strings.TrimPrefix(r.URL.Path, "/api/discord/snowflake/")
	if !snowflakeIDPattern.MatchString(id) {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "invalid_snowflake"})
		return
	}
	result, err := s.authMgr.GetDiscordSnowflake(id)
	if err != nil {
		status := http.StatusServiceUnavailable
		if errors.Is(err, auth.ErrNotAuthenticated) {
			status = http.StatusUnauthorized
		}
		w.WriteHeader(status)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "snowflake_lookup_unavailable"})
		return
	}
	_, _ = w.Write(result)
}
