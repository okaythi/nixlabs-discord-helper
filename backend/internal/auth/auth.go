package auth

import (
	"bytes"
	"context"
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
	updatedAtTime time.Time              `json:"-"`
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
			if t, tErr := time.Parse(time.RFC3339, a.UpdatedAt); tErr == nil {
				a.updatedAtTime = t
			}
			m.auth = &a
		}
	}
}

func (m *Manager) Save(a *StoredAuth) {
	if a != nil && a.updatedAtTime.IsZero() && a.UpdatedAt != "" {
		if t, tErr := time.Parse(time.RFC3339, a.UpdatedAt); tErr == nil {
			a.updatedAtTime = t
		}
	}
	m.mu.Lock()
	m.auth = a
	m.mu.Unlock()

	if a != nil {
		data, _ := json.Marshal(a)
		path := config.GetAuthFilePath()
		_ = os.WriteFile(path, data, 0600)
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
		updatedAt := a.updatedAtTime
		if updatedAt.IsZero() && a.UpdatedAt != "" {
			if parsed, err := time.Parse(time.RFC3339, a.UpdatedAt); err == nil {
				updatedAt = parsed
			}
		}
		if !updatedAt.IsZero() && a.User != nil && time.Since(updatedAt) >= 0 && time.Since(updatedAt) < 12*time.Hour {
			return true, a.User, a.Account
		}
	}

	user, account, err := m.fetchProfile(a.SessionCookie)
	if err != nil {
		return false, nil, nil
	}
	now := time.Now().UTC()
	m.Save(&StoredAuth{
		SessionCookie: a.SessionCookie,
		User:          user,
		Account:       account,
		UpdatedAt:     now.Format(time.RFC3339),
		updatedAtTime: now,
	})
	return true, user, account
}

func (m *Manager) fetchProfile(sessionCookie string) (map[string]interface{}, map[string]interface{}, error) {
	req, err := http.NewRequest(http.MethodGet, config.AccountsAPIBase+"/api/profile", nil)
	if err != nil {
		return nil, nil, err
	}
	req.Header.Set("Cookie", "_nixlabs_session="+sessionCookie)
	req.Header.Set("Accept", "application/json")
	resp, err := m.client.Do(req)
	if err != nil {
		return nil, nil, fmt.Errorf("could not verify account session: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, nil, fmt.Errorf("could not verify account session (HTTP %d)", resp.StatusCode)
	}
	var profile map[string]interface{}
	if json.NewDecoder(io.LimitReader(resp.Body, 64*1024)).Decode(&profile) != nil {
		return nil, nil, errors.New("accounts service returned an invalid profile")
	}
	user, _ := profile["user"].(map[string]interface{})
	if len(user) == 0 {
		user, _ = profile["profile"].(map[string]interface{})
	}
	if len(user) == 0 {
		return nil, nil, errors.New("accounts service did not return a user profile")
	}
	account, _ := profile["account"].(map[string]interface{})
	return user, account, nil
}

// DiscordAccountRequest uses the backend's session for the account operations
// allowlisted by the local server. Incoming browser credentials are not forwarded.
func (m *Manager) DiscordAccountRequest(ctx context.Context, method, endpoint string, body io.Reader) (*http.Response, error) {
	m.mu.RLock()
	a := m.auth
	m.mu.RUnlock()
	if a == nil || a.SessionCookie == "" {
		return nil, ErrNotAuthenticated
	}
	req, err := http.NewRequestWithContext(ctx, method, config.AccountsAPIBase+endpoint, body)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Cookie", "_nixlabs_session="+a.SessionCookie)
	req.Header.Set("Authorization", "Bearer "+a.SessionCookie)
	req.Header.Set("Accept", "application/json")
	if method == http.MethodPut {
		req.Header.Set("Content-Type", "application/json")
	}
	return m.client.Do(req)
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

// GetDiscordSnowflake asks Accounts to look up a snowflake with its global bot.
// The bot secret stays on Accounts; only selected public object fields are returned.
func (m *Manager) GetDiscordSnowflake(id string) ([]byte, error) {
	if ok, _, _ := m.GetSession(); !ok {
		return nil, ErrNotAuthenticated
	}
	m.mu.RLock()
	a := m.auth
	m.mu.RUnlock()
	if a == nil || a.SessionCookie == "" {
		return nil, ErrNotAuthenticated
	}
	req, err := http.NewRequest(http.MethodGet, config.AccountsAPIBase+"/api/third-party-auth/discord-helper/snowflakes/"+id, nil)
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
	if resp.StatusCode != http.StatusOK {
		return nil, ErrDiscordTokenUnavailable
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, 8192))
	if err != nil || !json.Valid(body) {
		return nil, ErrDiscordTokenUnavailable
	}
	return body, nil
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
	req.Header.Set("X-Nixlabs-Client", "discord-helper")
	req.Header.Set("X-Nixlabs-Client-Version", config.Version)
	req.Header.Set("X-Request-ID", fmt.Sprintf("dh-%d", time.Now().UnixNano()))

	resp, err := m.client.Do(req)
	if err != nil {
		return nil, nil, fmt.Errorf("could not connect to accounts service: %w", err)
	}
	defer resp.Body.Close()

	bodyBytes, err := io.ReadAll(io.LimitReader(resp.Body, 64*1024))
	if err != nil {
		return nil, nil, errors.New("could not read the accounts login response")
	}
	if resp.StatusCode != 200 {
		var errData map[string]string
		_ = json.Unmarshal(bodyBytes, &errData)
		requestID := resp.Header.Get("X-Request-ID")
		if requestID == "" {
			requestID = req.Header.Get("X-Request-ID")
		}
		if msg, ok := errData["error"]; ok {
			return nil, nil, fmt.Errorf("%s (HTTP %d; request %s)", msg, resp.StatusCode, requestID)
		}
		return nil, nil, fmt.Errorf("authentication failed (HTTP %d; request %s)", resp.StatusCode, requestID)
	}

	var loginResponse struct {
		Success           bool   `json:"success"`
		Error             string `json:"error"`
		MigrationRequired bool   `json:"migrationRequired"`
	}
	if json.Unmarshal(bodyBytes, &loginResponse) != nil {
		return nil, nil, errors.New("accounts service returned an invalid login response")
	}
	if !loginResponse.Success {
		if loginResponse.Error != "" {
			return nil, nil, errors.New(loginResponse.Error)
		}
		return nil, nil, errors.New("authentication failed")
	}
	if loginResponse.MigrationRequired {
		return nil, nil, errors.New("complete your account setup at accounts.nixlabs.tech before signing in")
	}

	var sessionCookie string
	for _, cookie := range resp.Cookies() {
		if cookie.Name == "_nixlabs_session" {
			sessionCookie = cookie.Value
			break
		}
	}
	if sessionCookie == "" {
		return nil, nil, errors.New("accounts service did not establish a session; please try signing in again")
	}

	userProfile, accountData, err := m.fetchProfile(sessionCookie)
	if err != nil {
		return nil, nil, err
	}
	if banned, _ := accountData["banned"].(bool); banned {
		return nil, nil, errors.New("this account has been suspended or banned")
	}

	now := time.Now().UTC()
	m.Save(&StoredAuth{
		SessionCookie: sessionCookie,
		User:          userProfile,
		Account:       accountData,
		UpdatedAt:     now.Format(time.RFC3339),
		updatedAtTime: now,
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
