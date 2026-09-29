package quests

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
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
	return &TaskRunner{
		client:    client,
		broadcast: broadcast,
	}
}

func (tr *TaskRunner) Enroll(ctx context.Context, questID string, trafficRaw, trafficSealed interface{}) {
	payload := map[string]interface{}{
		"location":                11,
		"is_targeted":             false,
		"metadata_raw":            nil,
		"metadata_sealed":         nil,
		"traffic_metadata_raw":    trafficRaw,
		"traffic_metadata_sealed": trafficSealed,
	}
	body, _ := json.Marshal(payload)

	req, err := tr.client.NewUserRequest("POST", fmt.Sprintf("/quests/%s/enroll", questID), bytes.NewReader(body))
	if err == nil {
		resp, rErr := tr.client.Do(req.WithContext(ctx))
		if rErr == nil {
			_ = resp.Body.Close()
		}
	}
}

func (tr *TaskRunner) CompleteVideo(ctx context.Context, qid, name, taskType string, secondsNeeded int, secondsDone float64, enrolledAtStr string) {
	var enrolledTs float64
	if enrolledAtStr != "" {
		if t, err := time.Parse(time.RFC3339, strings.ReplaceAll(enrolledAtStr, "Z", "+00:00")); err == nil {
			enrolledTs = float64(t.Unix())
		}
	}
	if enrolledTs == 0 {
		enrolledTs = float64(time.Now().Unix())
	}

	speed := 7.0
	interval := 1 * time.Second

	for secondsDone < float64(secondsNeeded) {
		select {
		case <-ctx.Done():
			return
		default:
		}

		maxAllowed := (float64(time.Now().Unix()) - enrolledTs) + 10.0
		diff := maxAllowed - secondsDone
		timestamp := secondsDone + speed

		if diff >= speed {
			body, _ := json.Marshal(map[string]interface{}{
				"timestamp": minFloat(float64(secondsNeeded), timestamp+rand.Float64()),
			})
			req, err := tr.client.NewUserRequest("POST", fmt.Sprintf("/quests/%s/video-progress", qid), bytes.NewReader(body))
			if err == nil {
				resp, rErr := tr.client.Do(req.WithContext(ctx))
				if rErr == nil {
					if resp.StatusCode == http.StatusOK {
						var resBody map[string]interface{}
						_ = json.NewDecoder(resp.Body).Decode(&resBody)
						_ = resp.Body.Close()

						secondsDone = minFloat(float64(secondsNeeded), timestamp)
						pct := (secondsDone / float64(secondsNeeded)) * 100

						tr.broadcast(ProgressEvent{
							QuestID:       qid,
							QuestName:     name,
							TaskType:      taskType,
							SecondsDone:   secondsDone,
							SecondsNeeded: secondsNeeded,
							Percent:       pct,
							Running:       true,
							StatusText:    fmt.Sprintf("Watching video: %s (%.0fs / %ds - %.0f%%)", name, secondsDone, secondsNeeded, pct),
						})

						if resBody["completed_at"] != nil {
							break
						}
					} else {
						_ = resp.Body.Close()
					}
				}
			}
		}

		if timestamp >= float64(secondsNeeded) {
			break
		}
		time.Sleep(interval)
	}

	// Final progress update
	body, _ := json.Marshal(map[string]interface{}{"timestamp": secondsNeeded})
	req, _ := tr.client.NewUserRequest("POST", fmt.Sprintf("/quests/%s/video-progress", qid), bytes.NewReader(body))
	if req != nil {
		if resp, err := tr.client.Do(req.WithContext(ctx)); err == nil {
			_ = resp.Body.Close()
		}
	}

	tr.broadcast(ProgressEvent{
		QuestID:       qid,
		QuestName:     name,
		TaskType:      taskType,
		SecondsDone:   float64(secondsNeeded),
		SecondsNeeded: secondsNeeded,
		Percent:       100,
		Running:       false,
		Completed:     true,
		StatusText:    fmt.Sprintf("Completed quest: %s!", name),
	})
}

func (tr *TaskRunner) CompleteHeartbeat(ctx context.Context, qid, name, taskType string, secondsNeeded int, secondsDone float64) {
	pid := rand.Intn(29000) + 1000
	streamKey := fmt.Sprintf("call:0:%d", pid)

	for secondsDone < float64(secondsNeeded) {
		select {
		case <-ctx.Done():
			return
		default:
		}

		pct := (secondsDone / float64(secondsNeeded)) * 100
		tr.broadcast(ProgressEvent{
			QuestID:       qid,
			QuestName:     name,
			TaskType:      taskType,
			SecondsDone:   secondsDone,
			SecondsNeeded: secondsNeeded,
			Percent:       pct,
			Running:       true,
			StatusText:    fmt.Sprintf("Playing %s (%.0fs / %ds - %.0f%%)", name, secondsDone, secondsNeeded, pct),
		})

		body, _ := json.Marshal(map[string]interface{}{
			"stream_key": streamKey,
			"terminal":   false,
		})
		req, err := tr.client.NewUserRequest("POST", fmt.Sprintf("/quests/%s/heartbeat", qid), bytes.NewReader(body))
		if err == nil {
			resp, rErr := tr.client.Do(req.WithContext(ctx))
			if rErr == nil {
				if resp.StatusCode == http.StatusOK {
					var resBody map[string]interface{}
					_ = json.NewDecoder(resp.Body).Decode(&resBody)
					_ = resp.Body.Close()

					if prog, ok := resBody["progress"].(map[string]interface{}); ok {
						if tProg, ok := prog[taskType].(map[string]interface{}); ok {
							if val, ok := tProg["value"].(float64); ok {
								secondsDone = val
							}
						}
					}

					if resBody["completed_at"] != nil || secondsDone >= float64(secondsNeeded) {
						break
					}
				} else if resp.StatusCode == http.StatusTooManyRequests {
					_ = resp.Body.Close()
					time.Sleep(10 * time.Second)
					continue
				} else {
					_ = resp.Body.Close()
				}
			}
		}

		for i := 0; i < 20 && secondsDone < float64(secondsNeeded); i++ {
			select {
			case <-ctx.Done():
				return
			default:
				time.Sleep(1 * time.Second)
				secondsDone++
				pct = minFloat(100, (secondsDone/float64(secondsNeeded))*100)
				tr.broadcast(ProgressEvent{
					QuestID:       qid,
					QuestName:     name,
					TaskType:      taskType,
					SecondsDone:   secondsDone,
					SecondsNeeded: secondsNeeded,
					Percent:       pct,
					Running:       true,
					StatusText:    fmt.Sprintf("Playing %s (%.0fs / %ds - %.0f%%)", name, secondsDone, secondsNeeded, pct),
				})
			}
		}
	}

	// Terminal beat
	body, _ := json.Marshal(map[string]interface{}{
		"stream_key": streamKey,
		"terminal":   true,
	})
	req, _ := tr.client.NewUserRequest("POST", fmt.Sprintf("/quests/%s/heartbeat", qid), bytes.NewReader(body))
	if req != nil {
		if resp, err := tr.client.Do(req.WithContext(ctx)); err == nil {
			_ = resp.Body.Close()
		}
	}

	tr.broadcast(ProgressEvent{
		QuestID:       qid,
		QuestName:     name,
		TaskType:      taskType,
		SecondsDone:   float64(secondsNeeded),
		SecondsNeeded: secondsNeeded,
		Percent:       100,
		Running:       false,
		Completed:     true,
		StatusText:    fmt.Sprintf("Completed quest: %s!", name),
	})
}

func (tr *TaskRunner) CompleteActivity(ctx context.Context, qid, name, taskType string, secondsNeeded int, secondsDone float64) {
	streamKey := "call:0:1"

	for secondsDone < float64(secondsNeeded) {
		select {
		case <-ctx.Done():
			return
		default:
		}

		pct := (secondsDone / float64(secondsNeeded)) * 100
		tr.broadcast(ProgressEvent{
			QuestID:       qid,
			QuestName:     name,
			TaskType:      taskType,
			SecondsDone:   secondsDone,
			SecondsNeeded: secondsNeeded,
			Percent:       pct,
			Running:       true,
			StatusText:    fmt.Sprintf("Playing %s (%.0fs / %ds - %.0f%%)", name, secondsDone, secondsNeeded, pct),
		})

		body, _ := json.Marshal(map[string]interface{}{
			"stream_key": streamKey,
			"terminal":   false,
		})
		req, err := tr.client.NewUserRequest("POST", fmt.Sprintf("/quests/%s/heartbeat", qid), bytes.NewReader(body))
		if err == nil {
			resp, rErr := tr.client.Do(req.WithContext(ctx))
			if rErr == nil {
				if resp.StatusCode == http.StatusOK {
					var resBody map[string]interface{}
					_ = json.NewDecoder(resp.Body).Decode(&resBody)
					_ = resp.Body.Close()

					if prog, ok := resBody["progress"].(map[string]interface{}); ok {
						if tProg, ok := prog["PLAY_ACTIVITY"].(map[string]interface{}); ok {
							if val, ok := tProg["value"].(float64); ok {
								secondsDone = val
							}
						}
					}

					if resBody["completed_at"] != nil || secondsDone >= float64(secondsNeeded) {
						break
					}
				} else {
					_ = resp.Body.Close()
				}
			}
		}

		for i := 0; i < 20 && secondsDone < float64(secondsNeeded); i++ {
			select {
			case <-ctx.Done():
				return
			default:
				time.Sleep(1 * time.Second)
				secondsDone++
				pct = minFloat(100, (secondsDone/float64(secondsNeeded))*100)
				tr.broadcast(ProgressEvent{
					QuestID:       qid,
					QuestName:     name,
					TaskType:      taskType,
					SecondsDone:   secondsDone,
					SecondsNeeded: secondsNeeded,
					Percent:       pct,
					Running:       true,
					StatusText:    fmt.Sprintf("Playing %s (%.0fs / %ds - %.0f%%)", name, secondsDone, secondsNeeded, pct),
				})
			}
		}
	}

	body, _ := json.Marshal(map[string]interface{}{
		"stream_key": streamKey,
		"terminal":   true,
	})
	req, _ := tr.client.NewUserRequest("POST", fmt.Sprintf("/quests/%s/heartbeat", qid), bytes.NewReader(body))
	if req != nil {
		if resp, err := tr.client.Do(req.WithContext(ctx)); err == nil {
			_ = resp.Body.Close()
		}
	}

	tr.broadcast(ProgressEvent{
		QuestID:       qid,
		QuestName:     name,
		TaskType:      taskType,
		SecondsDone:   float64(secondsNeeded),
		SecondsNeeded: secondsNeeded,
		Percent:       100,
		Running:       false,
		Completed:     true,
		StatusText:    fmt.Sprintf("Completed quest: %s!", name),
	})
}

func minFloat(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}
