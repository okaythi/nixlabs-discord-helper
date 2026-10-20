package server

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"

	"nixlabs-discord-helper/internal/quests"
)

func (s *Server) handleQuests(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if _, ok := s.ensureDiscordToken(w); !ok {
		return
	}
	forceRefresh := r.URL.Query().Get("refresh") == "true"
	questsList, err := s.questEngine.GetNormalizedQuests(forceRefresh)
	if err != nil {
		w.WriteHeader(http.StatusBadGateway)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}

	total := len(questsList)
	limitStr := r.URL.Query().Get("limit")
	offsetStr := r.URL.Query().Get("offset")

	if limitStr != "" {
		limit, _ := strconv.Atoi(limitStr)
		offset, _ := strconv.Atoi(offsetStr)
		if offset < 0 {
			offset = 0
		}
		if limit > 0 {
			end := offset + limit
			if offset >= total {
				questsList = []quests.QuestNormalized{}
			} else {
				if end > total {
					end = total
				}
				questsList = questsList[offset:end]
			}
			_ = json.NewEncoder(w).Encode(map[string]interface{}{
				"quests":   questsList,
				"total":    total,
				"limit":    limit,
				"offset":   offset,
				"has_more": offset+limit < total,
			})
			return
		}
	}

	_ = json.NewEncoder(w).Encode(map[string]interface{}{
		"quests":   questsList,
		"total":    total,
		"has_more": false,
	})
}

func (s *Server) handleQuestComplete(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	s.questStartMu.Lock()
	generation := s.questStartGeneration
	s.questStartMu.Unlock()
	if _, ok := s.ensureDiscordToken(w); !ok {
		return
	}
	var body struct {
		QuestID string `json:"quest_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.QuestID == "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "quest_id is required"})
		return
	}

	s.questStartMu.Lock()
	if generation != s.questStartGeneration {
		s.questStartMu.Unlock()
		w.WriteHeader(http.StatusConflict)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "quest_start_cancelled"})
		return
	}
	err := s.questEngine.StartQuest(body.QuestID)
	s.questStartMu.Unlock()
	if err != nil {
		w.WriteHeader(http.StatusConflict)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}

	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleQuestCompleteAll(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	s.questStartMu.Lock()
	generation := s.questStartGeneration
	s.questStartMu.Unlock()
	if _, ok := s.ensureDiscordToken(w); !ok {
		return
	}
	s.questStartMu.Lock()
	if generation != s.questStartGeneration {
		s.questStartMu.Unlock()
		w.WriteHeader(http.StatusConflict)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "quest_start_cancelled"})
		return
	}
	err := s.questEngine.StartAllQuests()
	s.questStartMu.Unlock()
	if err != nil {
		w.WriteHeader(http.StatusConflict)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleQuestCancel(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	s.questStartMu.Lock()
	s.questStartGeneration++
	s.questEngine.CancelRunning()
	s.questStartMu.Unlock()
	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleQuestProgress(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "Method not allowed"})
		return
	}
	if !s.questEngine.HasToken() {
		w.WriteHeader(http.StatusUnauthorized)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "discord_token_unavailable"})
		return
	}
	_ = json.NewEncoder(w).Encode(s.questEngine.CurrentProgress())
}

func (s *Server) handleQuestEvents(w http.ResponseWriter, r *http.Request) {
	if !s.questEngine.HasToken() {
		http.Error(w, `{"error":"discord_token_unavailable"}`, http.StatusUnauthorized)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "Streaming unsupported", http.StatusInternalServerError)
		return
	}

	ch := s.questEngine.Subscribe()
	defer s.questEngine.Unsubscribe(ch)

	for {
		select {
		case <-r.Context().Done():
			return
		case ev, open := <-ch:
			if !open {
				return
			}
			data, _ := json.Marshal(ev)
			fmt.Fprintf(w, "data: %s\n\n", string(data))
			flusher.Flush()
		}
	}
}
