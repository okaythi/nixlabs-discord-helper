package main

import (
	"embed"
	"flag"
	"io/fs"
	"log"
	"os"
	"os/signal"
	"syscall"

	"nixlabs-discord-helper/internal/auth"
	"nixlabs-discord-helper/internal/discord"
	"nixlabs-discord-helper/internal/quests"
	"nixlabs-discord-helper/internal/server"
)

//go:embed all:dist
var distEmbed embed.FS

func main() {
	port := flag.Int("port", 45731, "Port to listen on")
	flag.Parse()

	distFS, err := fs.Sub(distEmbed, "dist")
	if err != nil {
		log.Fatalf("Fatal: failed to load embedded static filesystem: %v", err)
	}

	authMgr := auth.NewManager()
	discordClient := discord.NewClient()
	questEngine := quests.NewEngine(discordClient)

	srv := server.NewServer(*port, authMgr, questEngine, distFS)

	// Graceful shutdown
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		log.Println("Shutting down nixlabs | Discord Helper server...")
		questEngine.CancelRunning()
		_ = srv.Close()
		os.Exit(0)
	}()

	if err := srv.Start(); err != nil {
		log.Fatalf("Server exited with error: %v", err)
	}
}
