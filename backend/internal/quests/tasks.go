package quests

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"strings"
	"time"

	"nixlabs-discord-helper/internal/discord"
)

type TaskRunner struct {
	client    *discord.Client
	broadcast func(ProgressEvent)
}

func NewTaskRunner(client *discord.Client, broadcast func(ProgressEvent)) *TaskRunner {
	return &TaskRunner{client: client, broadcast: broadcast}
}

func waitFor(ctx context.Context, duration time.Duration) error {
	timer := time.NewTimer(duration)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-timer.C:
		return nil
	}
}

// post sends one quest update. A rate limit is retried with Discord's cooldown;
// other failures are returned so they cannot be mistaken for progress.
func (tr *TaskRunner) post(ctx context.Context, path string, payload interface{}) (map[string]interface{}, error) {
	body, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}
	for attempt := 0; attempt < 3; attempt++ {
		req, err := tr.client.NewUserRequest(http.MethodPost, path, bytes.NewReader(body))
		if err != nil {
			return nil, err
		}
		resp, err := tr.client.Do(req.WithContext(ctx))
		if err != nil {
			return nil, err
		}
		if resp.StatusCode == http.StatusTooManyRequests {
			pause := parseRetryAfter(resp)
			resp.Body.Close()
			if err := waitFor(ctx, pause); err != nil {
				return nil, err
			}
			continue
		}
		responseBody, err := io.ReadAll(io.LimitReader(resp.Body, 4096))
		resp.Body.Close()
		if err != nil {
			return nil, err
		}
		if resp.StatusCode < 200 || resp.StatusCode >= 300 {
			return nil, fmt.Errorf("Discord returned HTTP %d: %s", resp.StatusCode, strings.TrimSpace(string(responseBody)))
		}
		if len(responseBody) == 0 {
			return nil, nil
		}
		var result map[string]interface{}
		if err := json.Unmarshal(responseBody, &result); err != nil {
			return nil, fmt.Errorf("invalid Discord response: %w", err)
		}
		return result, nil
	}
	return nil, fmt.Errorf("Discord rate limit persisted after three attempts")
}

func (tr *TaskRunner) Enroll(ctx context.Context, questID string, trafficRaw, trafficSealed interface{}) error {
	_, err := tr.post(ctx, fmt.Sprintf("/quests/%s/enroll", questID), map[string]interface{}{
		"location": 11, "is_targeted": false,
		"metadata_raw": nil, "metadata_sealed": nil,
		"traffic_metadata_raw": trafficRaw, "traffic_metadata_sealed": trafficSealed,
	})
	return err
}

func (tr *TaskRunner) progress(qid, name, taskType string, done float64, needed int, status string) {
	tr.broadcast(ProgressEvent{
		QuestID: qid, QuestName: name, TaskType: taskType,
		SecondsDone: done, SecondsNeeded: needed,
		Percent: minFloat(100, done/float64(needed)*100),
		Running: true, StatusText: status,
	})
}

func completed(body map[string]interface{}) bool {
	return getString(body, "completed_at", "completedAt") != ""
}

func (tr *TaskRunner) CompleteVideo(ctx context.Context, qid, name, taskType string, secondsNeeded int, secondsDone float64, enrolledAtStr string) error {
	enrolled := time.Now()
	if parsed, err := time.Parse(time.RFC3339Nano, enrolledAtStr); err == nil {
		enrolled = parsed
	}
	finalAttempts := 0
	for {
		if err := ctx.Err(); err != nil {
			return err
		}
		maxAllowed := time.Since(enrolled).Seconds() + 10
		timestamp := minFloat(float64(secondsNeeded), minFloat(secondsDone+7, maxAllowed))
		if timestamp <= secondsDone {
			if err := waitFor(ctx, time.Second); err != nil {
				return err
			}
			continue
		}
		body, err := tr.post(ctx, fmt.Sprintf("/quests/%s/video-progress", qid), map[string]interface{}{
			"timestamp": minFloat(float64(secondsNeeded), timestamp+rand.Float64()),
		})
		if err != nil {
			return err
		}
		secondsDone = timestamp
		tr.progress(qid, name, taskType, secondsDone, secondsNeeded, fmt.Sprintf("Watching video: %s (%.0fs / %ds)", name, secondsDone, secondsNeeded))
		if completed(body) {
			return nil
		}
		if secondsDone >= float64(secondsNeeded) {
			finalAttempts++
			if finalAttempts >= 3 {
				return fmt.Errorf("Discord did not confirm video completion")
			}
			secondsDone = float64(secondsNeeded) - 0.001 // Retry the final timestamp.
		}
		if err := waitFor(ctx, time.Second); err != nil {
			return err
		}
	}
}

func (tr *TaskRunner) CompleteHeartbeat(ctx context.Context, qid, name, taskType string, secondsNeeded int, secondsDone float64) error {
	streamKey := fmt.Sprintf("call:0:%d", rand.Intn(29000)+1000)
	return tr.completeHeartbeat(ctx, qid, name, taskType, secondsNeeded, secondsDone, streamKey)
}

func (tr *TaskRunner) CompleteActivity(ctx context.Context, qid, name, taskType string, secondsNeeded int, secondsDone float64) error {
	return tr.completeHeartbeat(ctx, qid, name, taskType, secondsNeeded, secondsDone, "call:0:1")
}

func (tr *TaskRunner) completeHeartbeat(ctx context.Context, qid, name, taskType string, secondsNeeded int, secondsDone float64, streamKey string) error {
	path := fmt.Sprintf("/quests/%s/heartbeat", qid)
	stalled := 0
	for {
		if err := ctx.Err(); err != nil {
			return err
		}
		body, err := tr.post(ctx, path, map[string]interface{}{"stream_key": streamKey, "terminal": false})
		if err != nil {
			return err
		}
		previous := secondsDone
		if progress := getMap(body, "progress"); progress != nil {
			if taskProgress := getMap(progress, taskType); taskProgress != nil {
				if value, ok := taskProgress["value"].(float64); ok {
					secondsDone = value
				}
			}
		}
		tr.progress(qid, name, taskType, secondsDone, secondsNeeded, fmt.Sprintf("Playing %s (%.0fs / %ds)", name, secondsDone, secondsNeeded))
		if completed(body) || secondsDone >= float64(secondsNeeded) {
			// The server has confirmed progress. Ending the heartbeat is best effort.
			_, _ = tr.post(ctx, path, map[string]interface{}{"stream_key": streamKey, "terminal": true})
			return ctx.Err()
		}
		if secondsDone <= previous {
			stalled++
			if stalled >= 6 {
				return fmt.Errorf("Discord heartbeat progress did not advance")
			}
		} else {
			stalled = 0
		}
		if err := waitFor(ctx, 20*time.Second); err != nil {
			return err
		}
	}
}

func minFloat(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}
