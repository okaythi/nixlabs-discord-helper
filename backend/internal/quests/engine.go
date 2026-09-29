package quests

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"nixlabs-discord-helper/internal/config"
	"nixlabs-discord-helper/internal/discord"
)

type QuestsCacheData struct {
	CachedAt   time.Time                `json:"cached_at"`
	ExpiresAt  time.Time                `json:"expires_at"`
	RawQuests  []map[string]interface{} `json:"raw_quests"`
	Normalized []QuestNormalized        `json:"normalized"`
}

type Engine struct {
	mu           sync.Mutex
	client       *discord.Client
	runner       *TaskRunner
	activeCancel context.CancelFunc
	isRunning    bool
	currentEvent *ProgressEvent

	cacheMu      sync.RWMutex
	cachedQuests []QuestNormalized
	cachedRaw    []map[string]interface{}
	cacheExpiry  time.Time

	subMu       sync.Mutex
	subscribers map[chan ProgressEvent]bool
}

func NewEngine(client *discord.Client) *Engine {
	e := &Engine{
		client:      client,
		subscribers: make(map[chan ProgressEvent]bool),
	}
	e.runner = NewTaskRunner(client, e.broadcast)
	e.loadCacheFromDisk()
	return e
}

func (e *Engine) loadCacheFromDisk() {
	cachePath := filepath.Join(config.GetConfigDir(), "quests_cache.json")
	dataBytes, err := os.ReadFile(cachePath)
	if err != nil {
		return
	}
	var cacheData QuestsCacheData
	if err := json.Unmarshal(dataBytes, &cacheData); err != nil {
		return
	}
	if time.Now().Before(cacheData.ExpiresAt) && len(cacheData.Normalized) > 0 {
		e.cacheMu.Lock()
		e.cachedQuests = cacheData.Normalized
		e.cachedRaw = cacheData.RawQuests
		e.cacheExpiry = cacheData.ExpiresAt
		e.cacheMu.Unlock()
	}
}

func (e *Engine) saveCache(raw []map[string]interface{}, normalized []QuestNormalized, ttl time.Duration) {
	e.cacheMu.Lock()
	e.cachedRaw = raw
	e.cachedQuests = normalized
	e.cacheExpiry = time.Now().Add(ttl)
	e.cacheMu.Unlock()

	e.persistCacheToDisk()
}

func (e *Engine) persistCacheToDisk() {
	cachePath := filepath.Join(config.GetConfigDir(), "quests_cache.json")
	e.cacheMu.RLock()
	cacheData := QuestsCacheData{
		CachedAt:   time.Now(),
		ExpiresAt:  e.cacheExpiry,
		RawQuests:  e.cachedRaw,
		Normalized: e.cachedQuests,
	}
	e.cacheMu.RUnlock()

	if b, err := json.MarshalIndent(cacheData, "", "  "); err == nil {
		_ = os.WriteFile(cachePath, b, 0600)
	}
}

func (e *Engine) MarkQuestCompleted(questID string) {
	e.cacheMu.Lock()
	for i := range e.cachedQuests {
		if e.cachedQuests[i].ID == questID {
			e.cachedQuests[i].Completed = true
			e.cachedQuests[i].SecondsDone = float64(e.cachedQuests[i].SecondsNeeded)
		}
	}
	for i := range e.cachedRaw {
		if qID, _ := e.cachedRaw[i]["id"].(string); qID == questID {
			us, _ := e.cachedRaw[i]["user_status"].(map[string]interface{})
			if us == nil {
				us = make(map[string]interface{})
				e.cachedRaw[i]["user_status"] = us
			}
			us["completed_at"] = time.Now().UTC().Format(time.RFC3339)
		}
	}
	e.cacheMu.Unlock()
	e.persistCacheToDisk()
}

func parseRetryAfter(resp *http.Response) time.Duration {
	if resp == nil {
		return 3 * time.Second
	}
	bodyBytes, _ := io.ReadAll(resp.Body)
	var rl struct {
		RetryAfter float64 `json:"retry_after"`
	}
	_ = json.Unmarshal(bodyBytes, &rl)
	sec := rl.RetryAfter
	if sec <= 0 {
		if hVal := resp.Header.Get("Retry-After"); hVal != "" {
			if parsed, pErr := strconv.ParseFloat(hVal, 64); pErr == nil && parsed > 0 {
				sec = parsed
			}
		}
	}
	if sec <= 0 {
		sec = 3.0
	}
	return time.Duration(sec*float64(time.Second)) + 500*time.Millisecond
}

func (e *Engine) FetchRawQuests(force bool) ([]map[string]interface{}, error) {
	if !force {
		e.cacheMu.RLock()
		if e.cachedRaw != nil && time.Now().Before(e.cacheExpiry) {
			raw := make([]map[string]interface{}, len(e.cachedRaw))
			copy(raw, e.cachedRaw)
			e.cacheMu.RUnlock()
			return raw, nil
		}
		e.cacheMu.RUnlock()
	}

	const maxRetries = 3
	var lastErr error
	var list []map[string]interface{}

	for attempt := 0; attempt < maxRetries; attempt++ {
		req, err := e.client.NewUserRequest("GET", "/quests/@me", nil)
		if err != nil {
			return nil, err
		}

		resp, err := e.client.Do(req)
		if err != nil {
			lastErr = err
			time.Sleep(time.Duration(attempt+1) * time.Second)
			continue
		}

		if resp.StatusCode == http.StatusOK {
			var result map[string]interface{}
			if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
				resp.Body.Close()
				return nil, err
			}
			resp.Body.Close()

			questsRaw, ok := result["quests"].([]interface{})
			if ok {
				for _, item := range questsRaw {
					if m, ok := item.(map[string]interface{}); ok {
						list = append(list, m)
					}
				}
			}
			lastErr = nil
			break
		}

		bodyBytes, _ := io.ReadAll(resp.Body)
		resp.Body.Close()

		if resp.StatusCode == http.StatusTooManyRequests {
			var rl struct {
				Message    string  `json:"message"`
				RetryAfter float64 `json:"retry_after"`
				Global     bool    `json:"global"`
				Code       int     `json:"code"`
			}
			_ = json.Unmarshal(bodyBytes, &rl)

			retrySec := rl.RetryAfter
			if retrySec <= 0 {
				if hVal := resp.Header.Get("Retry-After"); hVal != "" {
					if parsed, pErr := strconv.ParseFloat(hVal, 64); pErr == nil && parsed > 0 {
						retrySec = parsed
					}
				}
			}
			if retrySec <= 0 {
				retrySec = float64(attempt+1) * 3.0
			}

			sleepDur := time.Duration(retrySec*float64(time.Second)) + 500*time.Millisecond
			time.Sleep(sleepDur)
			lastErr = fmt.Errorf("discord API error 429: %s", string(bodyBytes))
			continue
		}

		lastErr = fmt.Errorf("discord API error %d: %s", resp.StatusCode, string(bodyBytes))
		break
	}

	if lastErr != nil {
		e.cacheMu.RLock()
		if e.cachedRaw != nil && len(e.cachedRaw) > 0 {
			raw := make([]map[string]interface{}, len(e.cachedRaw))
			copy(raw, e.cachedRaw)
			e.cacheMu.RUnlock()
			return raw, nil
		}
		e.cacheMu.RUnlock()
		return nil, lastErr
	}

	return list, nil
}

func (e *Engine) GetNormalizedQuests(force bool) ([]QuestNormalized, error) {
	if !force {
		e.cacheMu.RLock()
		if e.cachedQuests != nil && time.Now().Before(e.cacheExpiry) {
			result := make([]QuestNormalized, len(e.cachedQuests))
			copy(result, e.cachedQuests)
			e.cacheMu.RUnlock()
			return result, nil
		}
		e.cacheMu.RUnlock()
	}

	rawList, err := e.FetchRawQuests(force)
	if err != nil {
		e.cacheMu.RLock()
		if e.cachedQuests != nil && len(e.cachedQuests) > 0 {
			result := make([]QuestNormalized, len(e.cachedQuests))
			copy(result, e.cachedQuests)
			e.cacheMu.RUnlock()
			return result, nil
		}
		e.cacheMu.RUnlock()
		return nil, err
	}

	normalized := e.normalizeRawQuests(rawList)
	e.saveCache(rawList, normalized, 12*time.Hour)
	return normalized, nil
}

func (e *Engine) normalizeRawQuests(rawList []map[string]interface{}) []QuestNormalized {
	var normalized []QuestNormalized
	now := time.Now().UTC()

	for _, q := range rawList {
		id, _ := q["id"].(string)
		cfg, _ := q["config"].(map[string]interface{})
		msgs, _ := cfg["messages"].(map[string]interface{})
		assets, _ := cfg["assets"].(map[string]interface{})
		colors, _ := cfg["colors"].(map[string]interface{})
		rewardsCfg, _ := cfg["rewards_config"].(map[string]interface{})

		name := getString(msgs, "quest_name", "questName")
		gameTitle := getString(msgs, "game_title", "gameTitle")
		if name == "" {
			name = gameTitle
		}
		if name == "" {
			name = fmt.Sprintf("Quest#%s", id)
		}

		publisher := getString(msgs, "game_publisher", "gamePublisher")

		tc := getMap(cfg, "task_config_v2", "taskConfigV2", "task_config", "taskConfig")
		tasks := getMap(tc, "tasks")

		var taskType string
		var targetSeconds int
		for tName := range SupportedTasks {
			if taskObj, exists := tasks[tName].(map[string]interface{}); exists {
				taskType = tName
				if tgt, ok := taskObj["target"].(float64); ok {
					targetSeconds = int(tgt)
				}
				break
			}
		}

		us, _ := q["user_status"].(map[string]interface{})
		enrolledAt := getString(us, "enrolled_at", "enrolledAt")
		completedAt := getString(us, "completed_at", "completedAt")

		var secondsDone float64
		if progress, ok := us["progress"].(map[string]interface{}); ok {
			if taskProg, ok := progress[taskType].(map[string]interface{}); ok {
				if val, ok := taskProg["value"].(float64); ok {
					secondsDone = val
				}
			}
		}

		expiresAt := getString(cfg, "expires_at", "expiresAt")
		completable := taskType != ""
		if expiresAt != "" {
			if parsedExp, pErr := time.Parse(time.RFC3339, strings.ReplaceAll(expiresAt, "Z", "+00:00")); pErr == nil {
				if parsedExp.Before(now) {
					completable = false
				}
			}
		}

		var orbQuantity int
		if rewards, ok := rewardsCfg["rewards"].([]interface{}); ok && len(rewards) > 0 {
			if firstReward, ok := rewards[0].(map[string]interface{}); ok {
				if oq, ok := firstReward["premium_orb_quantity"].(float64); ok {
					orbQuantity = int(oq)
				} else if oq, ok := firstReward["orb_quantity"].(float64); ok {
					orbQuantity = int(oq)
				}
			}
		}

		var heroURL string
		if hero, ok := assets["hero"].(string); ok && hero != "" {
			heroURL = fmt.Sprintf("https://cdn.discordapp.com/%s", hero)
		}

		colorsMap := make(map[string]string)
		if p, ok := colors["primary"].(string); ok {
			colorsMap["primary"] = p
		}
		if s, ok := colors["secondary"].(string); ok {
			colorsMap["secondary"] = s
		}

		normalized = append(normalized, QuestNormalized{
			ID:            id,
			Name:          name,
			GameTitle:     gameTitle,
			GamePublisher: publisher,
			TaskType:      taskType,
			SecondsNeeded: targetSeconds,
			SecondsDone:   secondsDone,
			Enrolled:      enrolledAt != "",
			Completed:     completedAt != "",
			Completable:   completable,
			ExpiresAt:     expiresAt,
			OrbQuantity:   orbQuantity,
			HeroURL:       heroURL,
			Colors:        colorsMap,
		})
	}

	return normalized
}

func (e *Engine) Subscribe() chan ProgressEvent {
	ch := make(chan ProgressEvent, 16)
	e.subMu.Lock()
	e.subscribers[ch] = true
	e.subMu.Unlock()

	e.mu.Lock()
	if e.currentEvent != nil {
		ch <- *e.currentEvent
	}
	e.mu.Unlock()

	return ch
}

func (e *Engine) Unsubscribe(ch chan ProgressEvent) {
	e.subMu.Lock()
	delete(e.subscribers, ch)
	close(ch)
	e.subMu.Unlock()
}

func (e *Engine) broadcast(ev ProgressEvent) {
	e.mu.Lock()
	e.currentEvent = &ev
	e.mu.Unlock()

	e.subMu.Lock()
	defer e.subMu.Unlock()
	for ch := range e.subscribers {
		select {
		case ch <- ev:
		default:
		}
	}
}

func (e *Engine) CancelRunning() {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.activeCancel != nil {
		e.activeCancel()
		e.activeCancel = nil
	}
	e.isRunning = false
	if e.currentEvent != nil {
		ev := *e.currentEvent
		ev.Running = false
		ev.StatusText = "Cancelled"
		e.broadcast(ev)
	}
}

func (e *Engine) StartQuest(questID string) error {
	e.mu.Lock()
	if e.isRunning {
		e.mu.Unlock()
		return fmt.Errorf("a quest is already currently in progress")
	}
	ctx, cancel := context.WithCancel(context.Background())
	e.activeCancel = cancel
	e.isRunning = true
	e.mu.Unlock()

	go func() {
		defer func() {
			e.mu.Lock()
			e.isRunning = false
			e.activeCancel = nil
			e.mu.Unlock()
		}()

		rawList, err := e.FetchRawQuests(false)
		if err != nil {
			e.broadcast(ProgressEvent{Running: false, Error: err.Error()})
			return
		}

		var targetQuest map[string]interface{}
		for _, q := range rawList {
			if qID, _ := q["id"].(string); qID == questID {
				targetQuest = q
				break
			}
		}

		if targetQuest == nil {
			e.broadcast(ProgressEvent{Running: false, Error: "Quest not found"})
			return
		}

		e.runSingleQuest(ctx, targetQuest)
	}()

	return nil
}

func (e *Engine) StartAllQuests() error {
	e.mu.Lock()
	if e.isRunning {
		e.mu.Unlock()
		return fmt.Errorf("a quest is already currently in progress")
	}
	ctx, cancel := context.WithCancel(context.Background())
	e.activeCancel = cancel
	e.isRunning = true
	e.mu.Unlock()

	go func() {
		defer func() {
			e.mu.Lock()
			e.isRunning = false
			e.activeCancel = nil
			e.mu.Unlock()
		}()

		rawList, err := e.FetchRawQuests(false)
		if err != nil {
			e.broadcast(ProgressEvent{Running: false, Error: err.Error()})
			return
		}

		for _, q := range rawList {
			select {
			case <-ctx.Done():
				return
			default:
				us, _ := q["user_status"].(map[string]interface{})
				if getString(us, "completed_at", "completedAt") != "" {
					continue
				}
				e.runSingleQuest(ctx, q)
				time.Sleep(3 * time.Second)
			}
		}

		e.broadcast(ProgressEvent{
			Running:    false,
			Completed:  true,
			StatusText: "All eligible quests completed!",
		})
	}()

	return nil
}

func (e *Engine) runSingleQuest(ctx context.Context, quest map[string]interface{}) {
	qid, _ := quest["id"].(string)
	cfg, _ := quest["config"].(map[string]interface{})
	msgs, _ := cfg["messages"].(map[string]interface{})
	name := getString(msgs, "quest_name", "questName")
	if name == "" {
		name = getString(msgs, "game_title", "gameTitle")
	}

	tc := getMap(cfg, "task_config_v2", "taskConfigV2", "task_config", "taskConfig")
	tasks := getMap(tc, "tasks")

	var taskType string
	var secondsNeeded int
	for tName := range SupportedTasks {
		if taskObj, exists := tasks[tName].(map[string]interface{}); exists {
			taskType = tName
			if tgt, ok := taskObj["target"].(float64); ok {
				secondsNeeded = int(tgt)
			}
			break
		}
	}

	if taskType == "" || secondsNeeded == 0 {
		return
	}

	us, _ := quest["user_status"].(map[string]interface{})
	enrolledAt := getString(us, "enrolled_at", "enrolledAt")
	if enrolledAt == "" {
		e.broadcast(ProgressEvent{
			QuestID:       qid,
			QuestName:     name,
			TaskType:      taskType,
			SecondsNeeded: secondsNeeded,
			Running:       true,
			StatusText:    fmt.Sprintf("Enrolling in quest: %s...", name),
		})
		e.runner.Enroll(ctx, qid, quest["traffic_metadata_raw"], quest["traffic_metadata_sealed"])
		time.Sleep(2 * time.Second)
	}

	var secondsDone float64
	if progress, ok := us["progress"].(map[string]interface{}); ok {
		if taskProg, ok := progress[taskType].(map[string]interface{}); ok {
			if val, ok := taskProg["value"].(float64); ok {
				secondsDone = val
			}
		}
	}

	switch taskType {
	case "WATCH_VIDEO", "WATCH_VIDEO_ON_MOBILE":
		e.runner.CompleteVideo(ctx, qid, name, taskType, secondsNeeded, secondsDone, enrolledAt)
	case "PLAY_ON_DESKTOP", "STREAM_ON_DESKTOP":
		e.runner.CompleteHeartbeat(ctx, qid, name, taskType, secondsNeeded, secondsDone)
	case "PLAY_ACTIVITY":
		e.runner.CompleteActivity(ctx, qid, name, taskType, secondsNeeded, secondsDone)
	}

	e.MarkQuestCompleted(qid)
}

func getString(m map[string]interface{}, keys ...string) string {
	if m == nil {
		return ""
	}
	for _, k := range keys {
		if v, ok := m[k].(string); ok && v != "" {
			return v
		}
	}
	return ""
}

func getMap(m map[string]interface{}, keys ...string) map[string]interface{} {
	if m == nil {
		return nil
	}
	for _, k := range keys {
		if v, ok := m[k].(map[string]interface{}); ok {
			return v
		}
	}
	return nil
}
