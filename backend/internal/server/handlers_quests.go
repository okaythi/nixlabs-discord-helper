package server

import (
	"encoding/json"
	"fmt"
	"net/http"
)

func (s *Server) handleQuests(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	questsList, err := s.questEngine.GetNormalizedQuests()
	if err != nil {
		w.WriteHeader(http.StatusBadGateway)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]interface{}{
		"quests": questsList,
	})
}

func (s *Server) handleQuestComplete(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	var body struct {
		QuestID string `json:"quest_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.QuestID == "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "quest_id is required"})
		return
	}

	if err := s.questEngine.StartQuest(body.QuestID); err != nil {
		w.WriteHeader(http.StatusConflict)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}

	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleQuestCompleteAll(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if err := s.questEngine.StartAllQuests(); err != nil {
		w.WriteHeader(http.StatusConflict)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleQuestCancel(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	s.questEngine.CancelRunning()
	_ = json.NewEncoder(w).Encode(map[string]bool{"success": true})
}

func (s *Server) handleQuestEvents(w http.ResponseWriter, r *http.Request) {
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
