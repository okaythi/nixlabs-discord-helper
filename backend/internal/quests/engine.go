package quests

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"

	"nixlabs-discord-helper/internal/discord"
)

type Engine struct {
	mu           sync.Mutex
	client       *discord.Client
	runner       *TaskRunner
	activeCancel context.CancelFunc
	isRunning    bool
	currentEvent *ProgressEvent

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

func (e *Engine) FetchRawQuests() ([]map[string]interface{}, error) {
	req, err := e.client.NewUserRequest("GET", "/quests/@me", nil)
	if err != nil {
		return nil, err
	}

	resp, err := e.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		b, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("discord API error %d: %s", resp.StatusCode, string(b))
	}

	var result map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, err
	}

	questsRaw, ok := result["quests"].([]interface{})
	if !ok {
		return nil, nil
	}

	var list []map[string]interface{}
	for _, item := range questsRaw {
		if m, ok := item.(map[string]interface{}); ok {
			list = append(list, m)
		}
	}
	return list, nil
}

func (e *Engine) GetNormalizedQuests() ([]QuestNormalized, error) {
	rawList, err := e.FetchRawQuests()
	if err != nil {
		return nil, err
	}

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

	return normalized, nil
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

		rawList, err := e.FetchRawQuests()
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

		rawList, err := e.FetchRawQuests()
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
