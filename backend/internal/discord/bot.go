package discord

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"time"
)

type UserProfile struct {
	ID                 string `json:"id"`
	Username           string `json:"username"`
	GlobalName         string `json:"global_name"`
	Avatar             string `json:"avatar"`
	AvatarURL          string `json:"avatar_url"`
	Banner             string `json:"banner"`
	BannerURL          string `json:"banner_url"`
	BannerColor        string `json:"banner_color"`
	AccentColor        *int   `json:"accent_color"`
	CreatedAt          string `json:"created_at"`
	CreatedAtTimestamp int64  `json:"created_at_timestamp"`
}

// FetchUserProfile queries the currently authenticated user's own Discord profile.
func FetchUserProfile(token string) (*UserProfile, error) {
	client := &http.Client{Timeout: 10 * time.Second}
	url := "https://discord.com/api/v10/users/@me"

	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Authorization", token)

	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("failed to contact Discord: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("Discord profile request failed (%d)", resp.StatusCode)
	}

	var dUser map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&dUser); err != nil {
		return nil, err
	}

	idStr, _ := dUser["id"].(string)
	idNum, _ := strconv.ParseInt(idStr, 10, 64)

	// Snowflake algorithm: (snowflake >> 22) + 1420070400000
	createdAtMs := (idNum >> 22) + 1420070400000
	createdAt := time.UnixMilli(createdAtMs).UTC().Format(time.RFC3339)

	avatarHash, _ := dUser["avatar"].(string)
	var avatarURL string
	if avatarHash != "" {
		avatarURL = fmt.Sprintf("https://cdn.discordapp.com/avatars/%s/%s.png?size=256", idStr, avatarHash)
	}

	bannerHash, _ := dUser["banner"].(string)
	var bannerURL string
	if bannerHash != "" {
		bannerURL = fmt.Sprintf("https://cdn.discordapp.com/banners/%s/%s.png?size=1024", idStr, bannerHash)
	}

	bannerColor, _ := dUser["banner_color"].(string)
	if bannerColor == "" {
		bannerColor = "#97e4e0"
	}

	username, _ := dUser["username"].(string)
	globalName, _ := dUser["global_name"].(string)

	var accentColor *int
	if ac, ok := dUser["accent_color"].(float64); ok {
		val := int(ac)
		accentColor = &val
	}

	return &UserProfile{
		ID:                 idStr,
		Username:           username,
		GlobalName:         globalName,
		Avatar:             avatarHash,
		AvatarURL:          avatarURL,
		Banner:             bannerHash,
		BannerURL:          bannerURL,
		BannerColor:        bannerColor,
		AccentColor:        accentColor,
		CreatedAt:          createdAt,
		CreatedAtTimestamp: createdAtMs,
	}, nil
}
