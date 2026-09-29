package quests

var SupportedTasks = []string{
	"WATCH_VIDEO",
	"PLAY_ON_DESKTOP",
	"STREAM_ON_DESKTOP",
	"PLAY_ACTIVITY",
	"WATCH_VIDEO_ON_MOBILE",
}

// QuestNormalized represents a sanitized model returned to the frontend.
type QuestNormalized struct {
	ID            string            `json:"id"`
	Name          string            `json:"name"`
	GameTitle     string            `json:"game_title"`
	GamePublisher string            `json:"game_publisher,omitempty"`
	TaskType      string            `json:"task_type"`
	SecondsNeeded int               `json:"seconds_needed"`
	SecondsDone   float64           `json:"seconds_done"`
	Enrolled      bool              `json:"enrolled"`
	Completed     bool              `json:"completed"`
	Completable   bool              `json:"completable"`
	ExpiresAt     string            `json:"expires_at,omitempty"`
	OrbQuantity   int               `json:"orb_quantity,omitempty"`
	HeroURL       string            `json:"hero_url,omitempty"`
	Colors        map[string]string `json:"colors,omitempty"`
}

// ProgressEvent represents a real-time event delivered over SSE.
type ProgressEvent struct {
	QuestID       string  `json:"quest_id"`
	QuestName     string  `json:"quest_name"`
	TaskType      string  `json:"task_type"`
	SecondsDone   float64 `json:"seconds_done"`
	SecondsNeeded int     `json:"seconds_needed"`
	Percent       float64 `json:"percent"`
	StatusText    string  `json:"status_text"`
	Running       bool    `json:"running"`
	Completed     bool    `json:"completed"`
	Estimated     bool    `json:"estimated,omitempty"`
	Error         string  `json:"error,omitempty"`
}
