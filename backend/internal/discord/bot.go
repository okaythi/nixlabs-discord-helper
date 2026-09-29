package discord

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"time"

	"nixlabs-discord-helper/internal/config"
)

type UserProfile struct {
	ID                 string  `json:"id"`
	Username           string  `json:"username"`
	GlobalName         string  `json:"global_name"`
	Avatar             string  `json:"avatar"`
	AvatarURL          string  `json:"avatar_url"`
	Banner             string  `json:"banner"`
	BannerURL          string  `json:"banner_url"`
	BannerColor        string  `json:"banner_color"`
	AccentColor        *int    `json:"accent_color"`
	CreatedAt          string  `json:"created_at"`
	CreatedAtTimestamp int64   `json:"created_at_timestamp"`
}

// FetchUserProfileViaBot queries Discord Bot API and computes snowflake metadata.
func FetchUserProfileViaBot() (*UserProfile, error) {
	client := &http.Client{Timeout: 10 * time.Second}
	url := fmt.Sprintf("%s/users/%s", config.DiscordBotAPIBase, config.HardcodedUserID)

	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Authorization", fmt.Sprintf("Bot %s", config.BotToken))

	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("failed to contact Discord Bot API: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		b, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("discord Bot API error %d: %s", resp.StatusCode, string(b))
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
