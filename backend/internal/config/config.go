package config

import (
	"os"
	"path/filepath"
	"runtime"
	"sync"
)

const (
	DiscordAPIBase         = "https://discord.com/api/v9"
	AccountsAPIBase        = "https://accounts.nixlabs.tech"
	DefaultBuildNumber     = 504649
	GitHubLatestReleaseURL = "https://api.github.com/repos/okaythi/nixlabs-discord-helper/releases/latest"
)

// Version is set at build time via -ldflags; defaults to the current release version.
var Version = "1.0.15"

var (
	dirMu   sync.Mutex
	lastDir string
)

// GetConfigDir returns ~/.config/nixlabs-discord-helper (or %APPDATA%\nixlabs-discord-helper on Windows)
// respecting XDG_CONFIG_HOME and avoiding redundant MkdirAll/Chmod syscalls when unchanged.
func GetConfigDir() string {
	configHome := os.Getenv("XDG_CONFIG_HOME")
	if configHome == "" {
		if runtime.GOOS == "windows" {
			if appData, err := os.UserConfigDir(); err == nil && appData != "" {
				configHome = appData
			}
		}
		if configHome == "" {
			home, _ := os.UserHomeDir()
			configHome = filepath.Join(home, ".config")
		}
	}
	dir := filepath.Join(configHome, "nixlabs-discord-helper")

	dirMu.Lock()
	changed := (dir != lastDir)
	if changed {
		lastDir = dir
	}
	dirMu.Unlock()

	if changed {
		_ = os.MkdirAll(dir, 0700)
		_ = os.Chmod(dir, 0700)
	}
	return dir
}

// GetDataDir returns ~/.local/share/nixlabs-discord-helper respecting XDG specs.
func GetDataDir() string {
	dataHome := os.Getenv("XDG_DATA_HOME")
	if dataHome == "" {
		home, _ := os.UserHomeDir()
		dataHome = filepath.Join(home, ".local", "share")
	}
	dir := filepath.Join(dataHome, "nixlabs-discord-helper")
	_ = os.MkdirAll(dir, 0700)
	_ = os.Chmod(dir, 0700)
	return dir
}

// GetAuthFilePath returns the location of persistent session data.
func GetAuthFilePath() string {
	return filepath.Join(GetConfigDir(), "auth.json")
}
