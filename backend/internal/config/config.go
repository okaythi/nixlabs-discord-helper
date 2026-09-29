package config

import (
	"os"
	"path/filepath"
)

const (
	DiscordAPIBase     = "https://discord.com/api/v9"
	AccountsAPIBase    = "https://accounts.nixlabs.tech"
	DefaultBuildNumber = 504649
)

// GetConfigDir returns ~/.config/nixlabs-discord-helper respecting XDG specs.
func GetConfigDir() string {
	configHome := os.Getenv("XDG_CONFIG_HOME")
	if configHome == "" {
		home, _ := os.UserHomeDir()
		configHome = filepath.Join(home, ".config")
	}
	dir := filepath.Join(configHome, "nixlabs-discord-helper")
	_ = os.MkdirAll(dir, 0700)
	_ = os.Chmod(dir, 0700)
	return dir
}

// GetAuthFilePath returns the location of persistent session data.
func GetAuthFilePath() string {
	return filepath.Join(GetConfigDir(), "auth.json")
}
