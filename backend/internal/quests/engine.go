package quests

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
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
	tokenDigest  [32]byte

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
	return e
}

func (e *Engine) SetToken(token string) {
	digest := sha256.Sum256([]byte(token))
	e.mu.Lock()
	changed := e.tokenDigest != digest
	if changed {
		e.tokenDigest = digest
		e.client.SetToken(token)
	}
	e.mu.Unlock()
	if changed {
		e.cacheMu.Lock()
		e.cachedQuests = nil
		e.cachedRaw = nil
		e.cacheExpiry = time.Time{}
		e.cacheMu.Unlock()
	}
}

func (e *Engine) ClearToken() {
	e.CancelRunning()
	e.client.SetToken("")
	e.mu.Lock()
	e.tokenDigest = [32]byte{}
	e.mu.Unlock()
	e.cacheMu.Lock()
	e.cachedQuests = nil
	e.cachedRaw = nil
	e.cacheExpiry = time.Time{}
	e.cacheMu.Unlock()
}

func (e *Engine) HasToken() bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.tokenDigest != [32]byte{}
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
			us := getMap(e.cachedRaw[i], "user_status", "userStatus")
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

func (e *Engine) markQuestEnrolled(questID, enrolledAt string) {
	e.cacheMu.Lock()
	for i := range e.cachedQuests {
		if e.cachedQuests[i].ID == questID {
			e.cachedQuests[i].Enrolled = true
		}
	}
	for _, quest := range e.cachedRaw {
		if id, _ := quest["id"].(string); id == questID {
			status := getMap(quest, "user_status", "userStatus")
			if status == nil {
				status = make(map[string]interface{})
				quest["user_status"] = status
			}
			status["enrolled_at"] = enrolledAt
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
	return e.fetchRawQuests(context.Background(), force)
}

func (e *Engine) fetchRawQuests(ctx context.Context, force bool) ([]map[string]interface{}, error) {
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
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		req, err := e.client.NewUserRequest("GET", "/quests/@me", nil)
		if err != nil {
			return nil, err
		}

		resp, err := e.client.Do(req.WithContext(ctx))
		if err != nil {
			if ctx.Err() != nil {
				return nil, ctx.Err()
			}
			lastErr = err
			if err := waitFor(ctx, time.Duration(attempt+1)*time.Second); err != nil {
				return nil, err
			}
			continue
		}

		if resp.StatusCode == http.StatusOK {
			var result interface{}
			if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
				resp.Body.Close()
				return nil, err
			}
			resp.Body.Close()

			var questsRaw []interface{}
			switch data := result.(type) {
			case map[string]interface{}:
				questsRaw, _ = data["quests"].([]interface{})
			case []interface{}:
				questsRaw = data
			default:
				return nil, fmt.Errorf("unexpected Discord quest response")
			}
			for _, item := range questsRaw {
				if m, ok := item.(map[string]interface{}); ok {
					list = append(list, m)
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
			if err := waitFor(ctx, sleepDur); err != nil {
				return nil, err
			}
			lastErr = fmt.Errorf("discord API error 429: %s", string(bodyBytes))
			continue
		}

		lastErr = fmt.Errorf("discord API error %d: %s", resp.StatusCode, string(bodyBytes))
		break
	}

	if lastErr != nil && !force {
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
	if lastErr != nil {
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
	if err != nil && !force {
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
	if err != nil {
		return nil, err
	}

	normalized := e.normalizeRawQuests(rawList)
	e.saveCache(rawList, normalized, time.Minute)
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

		tc := getMap(cfg, "taskConfig", "task_config", "taskConfigV2", "task_config_v2")
		tasks := getMap(tc, "tasks")

		taskType, targetSeconds := selectedTask(tasks)

		us := getMap(q, "user_status", "userStatus")
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
		completable := isCompletable(q, now)

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
	if !e.isRunning || e.activeCancel == nil {
		e.mu.Unlock()
		return
	}
	e.activeCancel()
	ev := ProgressEvent{Running: false, StatusText: "Cancelled"}
	if e.currentEvent != nil {
		ev = *e.currentEvent
		ev.Running = false
		ev.Completed = false
		ev.StatusText = "Cancelled"
	}
	e.mu.Unlock()
	e.broadcast(ev)
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
	e.currentEvent = nil
	e.mu.Unlock()
	e.broadcast(ProgressEvent{QuestID: questID, Running: true, StatusText: "Loading quest"})

	go func() {
		defer func() {
			e.mu.Lock()
			e.isRunning = false
			e.activeCancel = nil
			e.mu.Unlock()
			if ctx.Err() != nil {
				e.broadcast(ProgressEvent{QuestID: questID, Running: false, StatusText: "Cancelled"})
			}
		}()

		rawList, err := e.fetchRawQuests(ctx, true)
		if err != nil {
			if ctx.Err() == nil {
				e.broadcast(ProgressEvent{Running: false, Error: err.Error()})
			}
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

		e.saveCache(rawList, e.normalizeRawQuests(rawList), time.Minute)
		if err := e.runSingleQuest(ctx, targetQuest, false); err != nil && ctx.Err() == nil {
			e.broadcast(ProgressEvent{QuestID: questID, Running: false, Error: err.Error()})
		}
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
	e.currentEvent = nil
	e.mu.Unlock()
	e.broadcast(ProgressEvent{Running: true, StatusText: "Loading quests"})

	go func() {
		defer func() {
			e.mu.Lock()
			e.isRunning = false
			e.activeCancel = nil
			e.mu.Unlock()
			if ctx.Err() != nil {
				e.broadcast(ProgressEvent{Running: false, StatusText: "Cancelled"})
			}
		}()

		rawList, err := e.fetchRawQuests(ctx, true)
		if err != nil {
			if ctx.Err() == nil {
				e.broadcast(ProgressEvent{Running: false, Error: err.Error()})
			}
			return
		}
		e.saveCache(rawList, e.normalizeRawQuests(rawList), time.Minute)

		failures := 0
		processed := 0
		for _, q := range rawList {
			select {
			case <-ctx.Done():
				return
			default:
				us := getMap(q, "user_status", "userStatus")
				if getString(us, "completed_at", "completedAt") != "" {
					continue
				}
				if !isCompletable(q, time.Now()) {
					continue
				}
				processed++
				if err := e.runSingleQuest(ctx, q, true); err != nil {
					if ctx.Err() != nil {
						return
					}
					failures++
					failed := e.normalizeRawQuests([]map[string]interface{}{q})[0]
					e.broadcast(ProgressEvent{
						QuestID: failed.ID, QuestName: failed.Name, TaskType: failed.TaskType,
						SecondsDone: failed.SecondsDone, SecondsNeeded: failed.SecondsNeeded,
						Running: true, Error: err.Error(), StatusText: "Quest failed; continuing",
					})
				}
				if err := waitFor(ctx, 3*time.Second); err != nil {
					return
				}
			}
		}

		ev := ProgressEvent{Running: false, Completed: failures == 0 && processed > 0, StatusText: "All eligible quests completed!"}
		if failures > 0 {
			ev.Error = fmt.Sprintf("%d of %d quests failed", failures, processed)
			ev.StatusText = ev.Error
		} else if processed == 0 {
			ev.StatusText = "No eligible quests found"
		}
		e.broadcast(ev)
	}()

	return nil
}

func (e *Engine) runSingleQuest(ctx context.Context, quest map[string]interface{}, batch bool) error {
	qid, _ := quest["id"].(string)
	cfg, _ := quest["config"].(map[string]interface{})
	msgs, _ := cfg["messages"].(map[string]interface{})
	name := getString(msgs, "quest_name", "questName")
	if name == "" {
		name = getString(msgs, "game_title", "gameTitle")
	}

	tc := getMap(cfg, "taskConfig", "task_config", "taskConfigV2", "task_config_v2")
	tasks := getMap(tc, "tasks")

	taskType, secondsNeeded := selectedTask(tasks)

	if taskType == "" || secondsNeeded <= 0 || !isCompletable(quest, time.Now()) {
		return fmt.Errorf("quest %s is not eligible for completion", qid)
	}

	us := getMap(quest, "user_status", "userStatus")
	if getString(us, "completed_at", "completedAt") != "" {
		return fmt.Errorf("quest %s is already completed", qid)
	}
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
		if err := e.runner.Enroll(ctx, qid, quest["traffic_metadata_raw"], quest["traffic_metadata_sealed"]); err != nil {
			return fmt.Errorf("enrolling %s: %w", name, err)
		}
		enrolledAt = time.Now().UTC().Format(time.RFC3339Nano)
		e.markQuestEnrolled(qid, enrolledAt)
		if err := waitFor(ctx, 2*time.Second); err != nil {
			return err
		}
	}

	var secondsDone float64
	if progress, ok := us["progress"].(map[string]interface{}); ok {
		if taskProg, ok := progress[taskType].(map[string]interface{}); ok {
			if val, ok := taskProg["value"].(float64); ok {
				secondsDone = val
			}
		}
	}

	var err error
	switch taskType {
	case "WATCH_VIDEO", "WATCH_VIDEO_ON_MOBILE":
		err = e.runner.CompleteVideo(ctx, qid, name, taskType, secondsNeeded, secondsDone, enrolledAt)
	case "PLAY_ON_DESKTOP", "STREAM_ON_DESKTOP":
		err = e.runner.CompleteHeartbeat(ctx, qid, name, taskType, secondsNeeded, secondsDone)
	case "PLAY_ACTIVITY":
		err = e.runner.CompleteActivity(ctx, qid, name, taskType, secondsNeeded, secondsDone)
	}
	if err != nil {
		return err
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	e.MarkQuestCompleted(qid)
	e.broadcast(ProgressEvent{QuestID: qid, QuestName: name, TaskType: taskType, SecondsDone: float64(secondsNeeded), SecondsNeeded: secondsNeeded, Percent: 100, Running: batch, Completed: true, StatusText: fmt.Sprintf("Completed quest: %s!", name)})
	return nil
}

func selectedTask(tasks map[string]interface{}) (string, int) {
	for _, name := range SupportedTasks {
		if task, ok := tasks[name].(map[string]interface{}); ok {
			if target, ok := task["target"].(float64); ok && target > 0 {
				return name, int(target)
			}
		}
	}
	return "", 0
}

func isCompletable(quest map[string]interface{}, now time.Time) bool {
	cfg := getMap(quest, "config")
	tc := getMap(cfg, "taskConfig", "task_config", "taskConfigV2", "task_config_v2")
	name, target := selectedTask(getMap(tc, "tasks"))
	if name == "" || target <= 0 {
		return false
	}
	if expires := getString(cfg, "expiresAt", "expires_at"); expires != "" {
		if when, err := time.Parse(time.RFC3339Nano, expires); err == nil && !when.After(now) {
			return false
		}
	}
	return true
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
