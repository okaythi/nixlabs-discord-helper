package quests

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"nixlabs-discord-helper/internal/discord"
)

type reviewRoundTripper func(*http.Request) (*http.Response, error)

func (fn reviewRoundTripper) RoundTrip(req *http.Request) (*http.Response, error) { return fn(req) }

var transportMu sync.RWMutex
var transportHandler = func(*http.Request) (*http.Response, error) { return mockResponse(404, ""), nil }

func TestMain(m *testing.M) {
	http.DefaultTransport = reviewRoundTripper(func(req *http.Request) (*http.Response, error) {
		transportMu.RLock()
		handler := transportHandler
		transportMu.RUnlock()
		return handler(req)
	})
	os.Exit(m.Run())
}
func setHandler(t *testing.T, fn func(*http.Request) (*http.Response, error)) {
	t.Helper()
	transportMu.Lock()
	transportHandler = fn
	transportMu.Unlock()
	t.Cleanup(func() {
		transportMu.Lock()
		transportHandler = func(*http.Request) (*http.Response, error) { return mockResponse(404, ""), nil }
		transportMu.Unlock()
	})
}
func mockResponse(status int, body string) *http.Response {
	return &http.Response{StatusCode: status, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(body))}
}
func client() *discord.Client { c := discord.NewClient(); c.SetToken("test-token"); return c }
func questJSON(t *testing.T, value string) map[string]interface{} {
	t.Helper()
	var q map[string]interface{}
	if err := json.Unmarshal([]byte(value), &q); err != nil {
		t.Fatal(err)
	}
	return q
}

func TestCurrentProgressSnapshot(t *testing.T) {
	engine := NewEngine(discord.NewClient())
	if engine.CurrentProgress().Running {
		t.Fatal("new engine should be idle")
	}
	engine.broadcast(ProgressEvent{QuestID: "quest-1", SecondsDone: 12, Running: true})
	snapshot := engine.CurrentProgress()
	if !snapshot.Running || snapshot.QuestID != "quest-1" || snapshot.SecondsDone != 12 {
		t.Fatalf("unexpected snapshot: %+v", snapshot)
	}
	snapshot.SecondsDone = 99
	if engine.CurrentProgress().SecondsDone != 12 {
		t.Fatal("snapshot must not mutate stored progress")
	}
}

func TestRejectedProgressNeverCompletes(t *testing.T) {
	setHandler(t, func(*http.Request) (*http.Response, error) { return mockResponse(403, `{"message":"rejected"}`), nil })
	for _, kind := range []string{"WATCH_VIDEO", "PLAY_ON_DESKTOP", "PLAY_ACTIVITY"} {
		t.Run(kind, func(t *testing.T) {
			events := 0
			runner := NewTaskRunner(client(), func(ev ProgressEvent) {
				if ev.Completed {
					events++
				}
			})
			var err error
			switch kind {
			case "WATCH_VIDEO":
				err = runner.CompleteVideo(context.Background(), "q", "Quest", kind, 1, 0, "")
			case "PLAY_ON_DESKTOP":
				err = runner.CompleteHeartbeat(context.Background(), "q", "Quest", kind, 1, 0)
			case "PLAY_ACTIVITY":
				err = runner.CompleteActivity(context.Background(), "q", "Quest", kind, 1, 0)
			}
			if err == nil || events != 0 {
				t.Fatalf("rejected progress: err=%v completed events=%d", err, events)
			}
		})
	}
}

func TestServerConfirmedProgress(t *testing.T) {
	setHandler(t, func(req *http.Request) (*http.Response, error) {
		if req.URL.Path == "/app" {
			return mockResponse(404, ""), nil
		}
		if req.URL.Path == "/api/v9/quests/q/heartbeat" {
			body, _ := io.ReadAll(req.Body)
			if strings.Contains(string(body), `"terminal":true`) {
				return mockResponse(204, ""), nil
			}
			return mockResponse(200, `{"progress":{"PLAY_ON_DESKTOP":{"value":2}},"completed_at":"2026-01-01T00:00:00Z"}`), nil
		}
		return mockResponse(200, `{"completed_at":"2026-01-01T00:00:00Z"}`), nil
	})
	runner := NewTaskRunner(client(), func(ProgressEvent) {})
	if err := runner.CompleteHeartbeat(context.Background(), "q", "Quest", "PLAY_ON_DESKTOP", 2, 0); err != nil {
		t.Fatal(err)
	}
	if err := runner.CompleteVideo(context.Background(), "q", "Quest", "WATCH_VIDEO", 1, 0, ""); err != nil {
		t.Fatal(err)
	}
}

func TestEstimatedHeartbeatProgressDoesNotComplete(t *testing.T) {
	var estimates []ProgressEvent
	runner := NewTaskRunner(nil, func(event ProgressEvent) { estimates = append(estimates, event) })
	ctx, cancel := context.WithTimeout(context.Background(), 1200*time.Millisecond)
	defer cancel()
	if err := runner.showHeartbeatEstimate(ctx, "q", "Quest", "PLAY_ACTIVITY", 5, 0, 20*time.Second); err != context.DeadlineExceeded {
		t.Fatalf("expected timeout, got %v", err)
	}
	if len(estimates) == 0 {
		t.Fatal("no estimated progress was emitted between heartbeats")
	}
	for _, event := range estimates {
		if !event.Estimated || event.Completed || !event.Running || event.SecondsDone >= float64(event.SecondsNeeded) {
			t.Fatalf("estimate must remain running and below completion: %+v", event)
		}
	}
}

func TestEnrollmentFailureStopsProgress(t *testing.T) {
	var paths []string
	setHandler(t, func(req *http.Request) (*http.Response, error) {
		paths = append(paths, req.URL.Path)
		return mockResponse(403, `{"message":"rejected"}`), nil
	})
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	e := NewEngine(client())
	q := questJSON(t, `{"id":"q","config":{"task_config":{"tasks":{"WATCH_VIDEO":{"target":5}}}}}`)
	if err := e.runSingleQuest(context.Background(), q, false); err == nil {
		t.Fatal("failed enrollment accepted")
	}
	for _, path := range paths {
		if strings.Contains(path, "video-progress") {
			t.Fatalf("progress sent after enrollment failure: %v", paths)
		}
	}
}

func TestCancellationDoesNotDeadlockOrMarkComplete(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	e := NewEngine(client())
	e.isRunning = true
	_, e.activeCancel = context.WithCancel(context.Background())
	e.currentEvent = &ProgressEvent{Running: true}
	done := make(chan struct{})
	go func() { e.CancelRunning(); close(done) }()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("cancel deadlocked")
	}
	q := questJSON(t, `{"id":"q","config":{"task_config":{"tasks":{"WATCH_VIDEO":{"target":60}}}},"userStatus":{"enrolledAt":"2026-01-01T00:00:00Z"}}`)
	e.saveCache([]map[string]interface{}{q}, e.normalizeRawQuests([]map[string]interface{}{q}), time.Minute)
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if err := e.runSingleQuest(ctx, q, false); err == nil {
		t.Fatal("cancelled quest returned success")
	}
	if e.cachedQuests[0].Completed {
		t.Fatal("cancelled quest marked completed")
	}
}

func TestTaskSelectionAndExpiry(t *testing.T) {
	q := questJSON(t, `{"id":"q","config":{"taskConfig":{"tasks":{"PLAY_ACTIVITY":{"target":9},"WATCH_VIDEO":{"target":5}}},"expiresAt":"2000-01-01T00:00:00Z"},"userStatus":{"enrolledAt":"2026-01-01T00:00:00Z"}}`)
	e := NewEngine(nil)
	for i := 0; i < 100; i++ {
		normalized := e.normalizeRawQuests([]map[string]interface{}{q})[0]
		if normalized.TaskType != "WATCH_VIDEO" || normalized.SecondsNeeded != 5 || !normalized.Enrolled || normalized.Completable {
			t.Fatalf("unexpected normalization: %+v", normalized)
		}
	}
	if err := e.runSingleQuest(context.Background(), q, false); err == nil {
		t.Fatal("expired quest executed")
	}
}

func TestTopLevelQuestArray(t *testing.T) {
	setHandler(t, func(req *http.Request) (*http.Response, error) {
		if req.URL.Path == "/api/v9/quests/@me" {
			return mockResponse(200, `[{"id":"q"}]`), nil
		}
		return mockResponse(404, ""), nil
	})
	e := NewEngine(client())
	quests, err := e.FetchRawQuests(true)
	if err != nil || len(quests) != 1 || quests[0]["id"] != "q" {
		t.Fatalf("quest array: %v %v", quests, err)
	}
}

func TestStopCancelsQuestFetch(t *testing.T) {
	started := make(chan struct{})
	setHandler(t, func(req *http.Request) (*http.Response, error) {
		if req.URL.Path == "/api/v9/quests/@me" {
			close(started)
			<-req.Context().Done()
			return nil, req.Context().Err()
		}
		return mockResponse(404, ""), nil
	})
	e := NewEngine(client())
	if err := e.StartQuest("q"); err != nil {
		t.Fatal(err)
	}
	select {
	case <-started:
	case <-time.After(time.Second):
		t.Fatal("fetch did not start")
	}
	e.CancelRunning()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		e.mu.Lock()
		running := e.isRunning
		e.mu.Unlock()
		if !running {
			return
		}
		time.Sleep(time.Millisecond)
	}
	t.Fatal("quest worker did not stop after cancellation")
}

func TestForcedRefreshDoesNotUseStaleCache(t *testing.T) {
	setHandler(t, func(*http.Request) (*http.Response, error) { return mockResponse(403, "rejected"), nil })
	e := NewEngine(client())
	e.cachedRaw = []map[string]interface{}{{"id": "stale"}}
	e.cachedQuests = []QuestNormalized{{ID: "stale"}}
	e.cacheExpiry = time.Now().Add(time.Hour)
	if _, err := e.GetNormalizedQuests(true); err == nil {
		t.Fatal("forced refresh used stale cache")
	}
}
