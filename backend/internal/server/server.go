package server

import (
	"fmt"
	"io"
	"io/fs"
	"log"
	"net/http"
	"strings"

	"nixlabs-discord-helper/internal/auth"
	"nixlabs-discord-helper/internal/quests"
)

type Server struct {
	port        int
	authMgr     *auth.Manager
	questEngine *quests.Engine
	distFS      fs.FS
	httpServer  *http.Server
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
	mux.HandleFunc("/api/auth/browser-session", s.handleBrowserSession)
	mux.HandleFunc("/auth/callback", s.handleAuthCallbackPage)

	// ── Discord & Quests Endpoints ──
	mux.HandleFunc("/api/discord/user", s.handleDiscordUser)
	mux.HandleFunc("/api/quests", s.handleQuests)
	mux.HandleFunc("/api/quests/complete", s.handleQuestComplete)
	mux.HandleFunc("/api/quests/complete-all", s.handleQuestCompleteAll)
	mux.HandleFunc("/api/quests/cancel", s.handleQuestCancel)
	mux.HandleFunc("/api/quests/events", s.handleQuestEvents)

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

	return mux
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
