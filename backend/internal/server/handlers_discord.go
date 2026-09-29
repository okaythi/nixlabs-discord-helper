package server

import (
	"encoding/json"
	"net/http"

	"nixlabs-discord-helper/internal/discord"
)

func (s *Server) handleDiscordUser(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	token, ok := s.ensureDiscordToken(w)
	if !ok {
		return
	}

	profile, err := discord.FetchUserProfile(token)
	if err != nil {
		w.WriteHeader(http.StatusBadGateway)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}

	_ = json.NewEncoder(w).Encode(profile)
}
