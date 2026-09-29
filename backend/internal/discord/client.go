package discord

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"regexp"
	"strconv"
	"sync"
	"time"

	"nixlabs-discord-helper/internal/config"
)

type Client struct {
	mu          sync.RWMutex
	httpClient  *http.Client
	buildNumber int
	superProps  string
	token       string
}

func (c *Client) SetToken(token string) {
	c.mu.Lock()
	c.token = token
	c.mu.Unlock()
}

func NewClient() *Client {
	c := &Client{
		httpClient:  &http.Client{Timeout: 30 * time.Second},
		buildNumber: config.DefaultBuildNumber,
	}
	go c.scrapeBuildNumber()
	return c
}

func (c *Client) scrapeBuildNumber() {
	bn := fetchBuildNumber()
	c.mu.Lock()
	c.buildNumber = bn
	c.superProps = buildSuperProperties(bn)
	c.mu.Unlock()
}

func fetchBuildNumber() int {
	client := &http.Client{Timeout: 15 * time.Second}
	req, err := http.NewRequest("GET", "https://discord.com/app", nil)
	if err != nil {
		return config.DefaultBuildNumber
	}
	req.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36")

	resp, err := client.Do(req)
	if err != nil || resp.StatusCode != 200 {
		return config.DefaultBuildNumber
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return config.DefaultBuildNumber
	}

	reAssets := regexp.MustCompile(`/assets/([a-f0-9]+)\.js`)
	matches := reAssets.FindAllStringSubmatch(string(body), -1)
	if len(matches) == 0 {
		return config.DefaultBuildNumber
	}

	start := len(matches) - 5
	if start < 0 {
		start = 0
	}

	reBN := regexp.MustCompile(`buildNumber["\s:]+["\s]*(\d{5,7})`)
	for i := len(matches) - 1; i >= start; i-- {
		assetURL := fmt.Sprintf("https://discord.com/assets/%s.js", matches[i][1])
		aReq, _ := http.NewRequest("GET", assetURL, nil)
		aReq.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36")
		aResp, aErr := client.Do(aReq)
		if aErr == nil && aResp.StatusCode == 200 {
			aBody, _ := io.ReadAll(aResp.Body)
			aResp.Body.Close()
			if m := reBN.FindStringSubmatch(string(aBody)); len(m) > 1 {
				if parsed, pErr := strconv.Atoi(m[1]); pErr == nil {
					return parsed
				}
			}
		}
	}
	return config.DefaultBuildNumber
}

func buildSuperProperties(buildNumber int) string {
	obj := map[string]interface{}{
		"os":                  "Windows",
		"browser":             "Discord Client",
		"release_channel":     "stable",
		"client_version":      "1.0.9175",
		"os_version":          "10.0.26100",
		"os_arch":             "x64",
		"app_arch":            "x64",
		"system_locale":       "en-US",
		"browser_user_agent":  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9175 Chrome/128.0.6613.186 Electron/32.2.7 Safari/537.36",
		"browser_version":     "32.2.7",
		"client_build_number": buildNumber,
		"native_build_number": 59498,
		"client_event_source": nil,
	}
	data, _ := json.Marshal(obj)
	return base64.StdEncoding.EncodeToString(data)
}

func (c *Client) NewUserRequest(method, path string, body io.Reader) (*http.Request, error) {
	url := fmt.Sprintf("%s%s", config.DiscordAPIBase, path)
	req, err := http.NewRequest(method, url, body)
	if err != nil {
		return nil, err
	}

	c.mu.RLock()
	sp := c.superProps
	token := c.token
	if sp == "" {
		sp = buildSuperProperties(c.buildNumber)
	}
	c.mu.RUnlock()
	if token == "" {
		return nil, fmt.Errorf("Discord token unavailable")
	}

	req.Header.Set("Authorization", token)
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "*/*")
	req.Header.Set("Accept-Language", "en-US,en;q=0.9")
	req.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9175 Chrome/128.0.6613.186 Electron/32.2.7 Safari/537.36")
	req.Header.Set("X-Super-Properties", sp)
	req.Header.Set("X-Discord-Locale", "en-US")
	req.Header.Set("X-Discord-Timezone", "UTC")
	req.Header.Set("Origin", "https://discord.com")
	req.Header.Set("Referer", "https://discord.com/channels/@me")

	return req, nil
}

func (c *Client) Do(req *http.Request) (*http.Response, error) {
	return c.httpClient.Do(req)
}
