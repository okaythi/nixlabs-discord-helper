package config

import (
	"os"
	"path/filepath"
)

const (
	DiscordAPIBase     = "https://discord.com/api/v9"
	DiscordBotAPIBase  = "https://discord.com/api/v10"
	AccountsAPIBase    = "https://accounts.nixlabs.tech"
	DefaultBuildNumber = 504649

	// Hardcoded credentials for quest automation & user bot querying
	HardcodedUserToken = "MTE0NzgwNzAzMDc1MzYyODIwMA.Gi_EVk.7Sa9ZMoY_y-O9gCE_Iob9-lnIS1StR_x51IiGU"
	HardcodedUserID    = "1147807030753628200"
	BotToken           = "MTU1NDQ2MjEyMjA1NzEzODIxNg.GbTsnG.JwJDu-imiU4QfouQZIxpcSfiGlXWL9O3g_3PA8"
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
	return dir
}

// GetAuthFilePath returns the location of persistent session data.
func GetAuthFilePath() string {
	return filepath.Join(GetConfigDir(), "auth.json")
}
