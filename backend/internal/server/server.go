package server

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net/http"
	"strings"
	"sync"

	"nixlabs-discord-helper/internal/auth"
	"nixlabs-discord-helper/internal/quests"
)

type Server struct {
	port                 int
	authMgr              *auth.Manager
	questEngine          *quests.Engine
	questStartMu         sync.Mutex
	questStartGeneration uint64
	distFS               fs.FS
	httpServer           *http.Server
}

func NewServer(port int, authMgr *auth.Manager, questEngine *quests.Engine, distFS fs.FS) *Server {
	return &Server{
		port:        port,
		authMgr:     authMgr,
		questEngine: questEngine,
		distFS:      distFS,
	}
}

func (s *Server) Routes() http.Handler {
	mux := http.NewServeMux()

	// ── Auth Endpoints ──
	mux.HandleFunc("/api/auth/session", s.handleSession)
	mux.HandleFunc("/api/auth/login", s.handleLogin)
	mux.HandleFunc("/api/auth/logout", s.handleLogout)
	mux.HandleFunc("/api/auth/register-url", s.handleRegisterURL)
	mux.HandleFunc("/api/auth/open-browser", s.handleOpenBrowser)
	mux.HandleFunc("/auth/callback", s.handleAuthCallbackPage)

	// ── Discord & Quests Endpoints ──
	mux.HandleFunc("/api/discord/user", s.handleDiscordUser)
	mux.HandleFunc("/api/discord/snowflake/", s.handleDiscordSnowflake)
	mux.HandleFunc("/api/quests", s.handleQuests)
	mux.HandleFunc("/api/quests/complete", s.handleQuestComplete)
	mux.HandleFunc("/api/quests/complete-all", s.handleQuestCompleteAll)
	mux.HandleFunc("/api/quests/cancel", s.handleQuestCancel)
	mux.HandleFunc("/api/quests/events", s.handleQuestEvents)
	mux.HandleFunc("/api/quests/progress", s.handleQuestProgress)

	// ── SPA Static Assets with HTML5 History Fallback ──
	fileServer := http.FileServer(http.FS(s.distFS))
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/api/") {
			http.NotFound(w, r)
			return
		}

		cleanPath := strings.TrimPrefix(r.URL.Path, "/")
		if cleanPath == "" {
			cleanPath = "index.html"
		}

		if _, err := s.distFS.Open(cleanPath); err != nil {
			indexFile, err := s.distFS.Open("index.html")
			if err != nil {
				http.NotFound(w, r)
				return
			}
			defer indexFile.Close()
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			_, _ = io.Copy(w, indexFile)
			return
		}

		fileServer.ServeHTTP(w, r)
	})

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		localOrigin := fmt.Sprintf("http://127.0.0.1:%d", s.port)
		if r.Host != fmt.Sprintf("127.0.0.1:%d", s.port) ||
			(r.Header.Get("Origin") != "" && r.Header.Get("Origin") != localOrigin) ||
			(r.Header.Get("Sec-Fetch-Site") != "" && r.Header.Get("Sec-Fetch-Site") != "same-origin" && r.Header.Get("Sec-Fetch-Site") != "none") {
			http.Error(w, "Forbidden", http.StatusForbidden)
			return
		}
		mux.ServeHTTP(w, r)
	})
}

func (s *Server) ensureDiscordToken(w http.ResponseWriter) (string, bool) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	token, err := s.authMgr.GetDiscordToken()
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
		return "", false
	}
	s.questEngine.SetToken(token)
	return token, true
}

func (s *Server) Start() error {
	addr := fmt.Sprintf("127.0.0.1:%d", s.port)
	s.httpServer = &http.Server{
		Addr:    addr,
		Handler: s.Routes(),
	}

	log.Printf("nixlabs | Discord Helper server running at http://%s", addr)
	return s.httpServer.ListenAndServe()
}

func (s *Server) Close() error {
	if s.httpServer != nil {
		return s.httpServer.Close()
	}
	return nil
}
