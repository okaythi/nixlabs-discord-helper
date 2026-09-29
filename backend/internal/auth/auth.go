package auth

import (
	"bytes"
	"encoding/json"
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
		_ = os.WriteFile(config.GetAuthFilePath(), data, 0600)
	} else {
		_ = os.Remove(config.GetAuthFilePath())
	}
}

func (m *Manager) GetSession() (bool, map[string]interface{}, map[string]interface{}) {
	m.mu.RLock()
	a := m.auth
	m.mu.RUnlock()

	if a == nil || (a.SessionCookie == "" && a.User == nil) {
		return false, nil, nil
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

						a.User = userObj
						a.Account = accObj
						a.UpdatedAt = time.Now().UTC().Format(time.RFC3339)
						m.Save(a)

						return true, userObj, accObj
					}
				}
			}
		}
	}

	if a.User != nil {
		return true, a.User, a.Account
	}
	return false, nil, nil
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
