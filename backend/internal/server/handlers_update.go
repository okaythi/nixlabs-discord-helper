package server

import (
	"encoding/json"
	"fmt"
	"net/http"
)

func (s *Server) handleUpdateStatus(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "Method not allowed"})
		return
	}

	force := r.URL.Query().Get("check") == "true"
	status, err := s.updateMgr.CheckForUpdate(r.Context(), force)
	if err != nil && !status.Available && status.Phase == "idle" {
		// Non-fatal: still return current status so UI doesn't error out on offline launch.
		_ = json.NewEncoder(w).Encode(status)
		return
	}
	_ = json.NewEncoder(w).Encode(status)
}

func (s *Server) handleUpdateStart(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "Method not allowed"})
		return
	}

	status, err := s.updateMgr.StartUpdate(r.Context())
	if err != nil {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
		return
	}
	w.WriteHeader(http.StatusAccepted)
	_ = json.NewEncoder(w).Encode(status)
}

func (s *Server) handleUpdateDismiss(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "Method not allowed"})
		return
	}

	status := s.updateMgr.Dismiss()
	_ = json.NewEncoder(w).Encode(status)
}

func (s *Server) handleUpdateEvents(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
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

	ch := s.updateMgr.Subscribe()
	defer s.updateMgr.Unsubscribe(ch)

	for {
		select {
		case <-r.Context().Done():
			return
		case st, open := <-ch:
			if !open {
				return
			}
			data, _ := json.Marshal(st)
			fmt.Fprintf(w, "data: %s\n\n", string(data))
			flusher.Flush()
		}
	}
}
