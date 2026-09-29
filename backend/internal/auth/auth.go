package auth

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	"nixlabs-discord-helper/internal/config"
)

type StoredAuth struct {
	SessionCookie string                 `json:"session_cookie"`
	User          map[string]interface{} `json:"user,omitempty"`
	Account       map[string]interface{} `json:"account,omitempty"`
	UpdatedAt     string                 `json:"updated_at"`
}

type Manager struct {
	mu     sync.RWMutex
	auth   *StoredAuth
	client *http.Client
}

var (
	ErrNotAuthenticated        = errors.New("Nixlabs session is not authenticated")
	ErrDiscordTokenMissing     = errors.New("Discord token has not been provided")
	ErrDiscordTokenInvalid     = errors.New("Discord token is invalid")
	ErrDiscordTokenUnavailable = errors.New("Discord token service is unavailable")
)

func NewManager() *Manager {
	m := &Manager{
		client: &http.Client{Timeout: 15 * time.Second},
	}
	m.load()
	return m
}

func (m *Manager) load() {
	m.mu.Lock()
	defer m.mu.Unlock()

	data, err := os.ReadFile(config.GetAuthFilePath())
	if err == nil {
		var a StoredAuth
		if json.Unmarshal(data, &a) == nil {
			m.auth = &a
		}
	}
}

func (m *Manager) Save(a *StoredAuth) {
	m.mu.Lock()
	m.auth = a
	m.mu.Unlock()

	if a != nil {
		data, _ := json.MarshalIndent(a, "", "  ")
		path := config.GetAuthFilePath()
		_ = os.WriteFile(path, data, 0600)
		_ = os.Chmod(path, 0600)
	} else {
		_ = os.Remove(config.GetAuthFilePath())
	}
}

func (m *Manager) GetSession(forceRefresh ...bool) (bool, map[string]interface{}, map[string]interface{}) {
	m.mu.RLock()
	a := m.auth
	m.mu.RUnlock()

	if a == nil || a.SessionCookie == "" {
		return false, nil, nil
	}

	// Reuse the last successful profile for 12 hours, including across app restarts.
	// Explicit Account refresh bypasses this cache.
	if len(forceRefresh) == 0 || !forceRefresh[0] {
		updatedAt, err := time.Parse(time.RFC3339, a.UpdatedAt)
		if err == nil && a.User != nil && time.Since(updatedAt) >= 0 && time.Since(updatedAt) < 12*time.Hour {
			return true, a.User, a.Account
		}
	}

	// Verify or refresh against accounts.nixlabs.tech if cookie is present
	if a.SessionCookie != "" {
		req, err := http.NewRequest("GET", config.AccountsAPIBase+"/api/profile", nil)
		if err == nil {
			req.Header.Set("Cookie", fmt.Sprintf("_nixlabs_session=%s", a.SessionCookie))
			req.Header.Set("Accept", "application/json")
			resp, rErr := m.client.Do(req)
			if rErr == nil {
				defer resp.Body.Close()
				if resp.StatusCode == 200 {
					var profileResp map[string]interface{}
					if json.NewDecoder(resp.Body).Decode(&profileResp) == nil {
						userObj, _ := profileResp["user"].(map[string]interface{})
						if userObj == nil {
							userObj, _ = profileResp["profile"].(map[string]interface{})
						}
						accObj, _ := profileResp["account"].(map[string]interface{})

						m.Save(&StoredAuth{
							SessionCookie: a.SessionCookie,
							User:          userObj,
							Account:       accObj,
							UpdatedAt:     time.Now().UTC().Format(time.RFC3339),
						})

						return true, userObj, accObj
					}
				}
			}
		}
	}

	return false, nil, nil
}

// GetDiscordToken verifies the live Nixlabs session before explicitly requesting
// the caller's Discord token. The token is returned only to the local backend.
func (m *Manager) GetDiscordToken() (string, error) {
	if ok, _, _ := m.GetSession(); !ok {
		return "", ErrNotAuthenticated
	}
	m.mu.RLock()
	a := m.auth
	m.mu.RUnlock()
	if a == nil || a.SessionCookie == "" {
		return "", ErrNotAuthenticated
	}
	req, err := http.NewRequest(http.MethodPost, config.AccountsAPIBase+"/api/third-party-auth/discord-helper/reveal", nil)
	if err != nil {
		return "", ErrDiscordTokenUnavailable
	}
	req.Header.Set("Authorization", "Bearer "+a.SessionCookie)
	req.Header.Set("Cookie", "_nixlabs_session="+a.SessionCookie)
	req.Header.Set("Accept", "application/json")
	resp, err := m.client.Do(req)
	if err != nil {
		return "", ErrDiscordTokenUnavailable
	}
	defer resp.Body.Close()
	switch resp.StatusCode {
	case http.StatusUnauthorized:
		return "", ErrNotAuthenticated
	case http.StatusNotFound:
		return "", ErrDiscordTokenMissing
	case http.StatusUnprocessableEntity:
		return "", ErrDiscordTokenInvalid
	case http.StatusOK:
		var body struct {
			Token string `json:"token"`
		}
		if json.NewDecoder(io.LimitReader(resp.Body, 1024)).Decode(&body) != nil || body.Token == "" {
			return "", ErrDiscordTokenUnavailable
		}
		return body.Token, nil
	default:
		return "", ErrDiscordTokenUnavailable
	}
}

// GetDiscordBotProfile asks Accounts to perform the global bot-token lookup.
// The bot secret never leaves Accounts or reaches this desktop process.
func (m *Manager) GetDiscordBotProfile(forceProfileRefresh ...bool) ([]byte, error) {
	if ok, _, _ := m.GetSession(forceProfileRefresh...); !ok {
		return nil, ErrNotAuthenticated
	}
	m.mu.RLock()
	a := m.auth
	m.mu.RUnlock()
	if a == nil || a.SessionCookie == "" {
		return nil, ErrNotAuthenticated
	}
	req, err := http.NewRequest(http.MethodGet, config.AccountsAPIBase+"/api/third-party-auth/discord-helper/bot-profile", nil)
	if err != nil {
		return nil, ErrDiscordTokenUnavailable
	}
	req.Header.Set("Authorization", "Bearer "+a.SessionCookie)
	req.Header.Set("Cookie", "_nixlabs_session="+a.SessionCookie)
	req.Header.Set("Accept", "application/json")
	resp, err := m.client.Do(req)
	if err != nil {
		return nil, ErrDiscordTokenUnavailable
	}
	defer resp.Body.Close()
	switch resp.StatusCode {
	case http.StatusUnauthorized:
		return nil, ErrNotAuthenticated
	case http.StatusNotFound:
		return nil, ErrDiscordTokenMissing
	case http.StatusUnprocessableEntity:
		return nil, ErrDiscordTokenInvalid
	case http.StatusOK:
		body, err := io.ReadAll(io.LimitReader(resp.Body, 8192))
		if err != nil || !json.Valid(body) {
			return nil, ErrDiscordTokenUnavailable
		}
		return body, nil
	default:
		return nil, ErrDiscordTokenUnavailable
	}
}

func (m *Manager) LoginInApp(identifier, password string) (map[string]interface{}, map[string]interface{}, error) {
	payload, _ := json.Marshal(map[string]string{
		"identifier": identifier,
		"password":   password,
	})

	req, err := http.NewRequest("POST", config.AccountsAPIBase+"/api/login", bytes.NewReader(payload))
	if err != nil {
		return nil, nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")

	resp, err := m.client.Do(req)
	if err != nil {
		return nil, nil, fmt.Errorf("could not connect to accounts service: %w", err)
	}
	defer resp.Body.Close()

	bodyBytes, _ := io.ReadAll(resp.Body)
	if resp.StatusCode != 200 {
		var errData map[string]string
		_ = json.Unmarshal(bodyBytes, &errData)
		if msg, ok := errData["error"]; ok {
			return nil, nil, fmt.Errorf("%s", msg)
		}
		return nil, nil, fmt.Errorf("authentication failed (%d)", resp.StatusCode)
	}

	// Extract session cookie
	var sessionCookie string
	for _, cookie := range resp.Cookies() {
		if cookie.Name == "_nixlabs_session" {
			sessionCookie = cookie.Value
			break
		}
	}
	if sessionCookie == "" {
		sc := resp.Header.Get("Set-Cookie")
		if idx := strings.Index(sc, "_nixlabs_session="); idx != -1 {
			val := sc[idx+len("_nixlabs_session="):]
			if semi := strings.Index(val, ";"); semi != -1 {
				val = val[:semi]
			}
			sessionCookie = val
		}
	}

	// Fetch user profile with session cookie
	var userProfile map[string]interface{}
	var accountData map[string]interface{}
	if sessionCookie != "" {
		pReq, _ := http.NewRequest("GET", config.AccountsAPIBase+"/api/profile", nil)
		pReq.Header.Set("Cookie", fmt.Sprintf("_nixlabs_session=%s", sessionCookie))
		pResp, pErr := m.client.Do(pReq)
		if pErr == nil {
			defer pResp.Body.Close()
			var pr map[string]interface{}
			if json.NewDecoder(pResp.Body).Decode(&pr) == nil {
				userProfile, _ = pr["user"].(map[string]interface{})
				if userProfile == nil {
					userProfile, _ = pr["profile"].(map[string]interface{})
				}
				accountData, _ = pr["account"].(map[string]interface{})
			}
		}
	}

	m.Save(&StoredAuth{
		SessionCookie: sessionCookie,
		User:          userProfile,
		Account:       accountData,
		UpdatedAt:     time.Now().UTC().Format(time.RFC3339),
	})

	return userProfile, accountData, nil
}

func (m *Manager) CreateRegisterURL(port int) string {
	localOrigin := fmt.Sprintf("http://127.0.0.1:%d", port)
	returnTo := fmt.Sprintf("%s/auth/callback", localOrigin)

	payload, _ := json.Marshal(map[string]string{
		"returnTo": returnTo,
	})

	req, err := http.NewRequest("POST", config.AccountsAPIBase+"/api/handshake", bytes.NewReader(payload))
	if err == nil {
		req.Header.Set("Origin", localOrigin)
		req.Header.Set("Content-Type", "application/json")
		resp, rErr := m.client.Do(req)
		if rErr == nil && resp.StatusCode == 200 {
			defer resp.Body.Close()
			var res struct {
				URL string `json:"url"`
			}
			if json.NewDecoder(resp.Body).Decode(&res) == nil && res.URL != "" {
				sep := "?"
				if strings.Contains(res.URL, "?") {
					sep = "&"
				}
				return fmt.Sprintf("%s%smode=signup#signup", res.URL, sep)
			}
		}
	}
	return config.AccountsAPIBase + "/login?mode=signup#signup"
}

func (m *Manager) Logout() {
	m.mu.RLock()
	a := m.auth
	m.mu.RUnlock()

	if a != nil && a.SessionCookie != "" {
		go func(cookie string) {
			req, _ := http.NewRequest("POST", config.AccountsAPIBase+"/api/logout", nil)
			req.Header.Set("Cookie", fmt.Sprintf("_nixlabs_session=%s", cookie))
			resp, _ := m.client.Do(req)
			if resp != nil {
				resp.Body.Close()
			}
		}(a.SessionCookie)
	}

	m.Save(nil)
}
