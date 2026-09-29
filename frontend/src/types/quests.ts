export interface QuestTask {
  type: string;
  target: number;
  applications?: Array<{ id: string }>;
}

export interface Quest {
  id: string;
  name: string;
  game_title: string;
  game_publisher?: string;
  task_type: string;
  seconds_needed: number;
  seconds_done: number;
  enrolled: boolean;
  completed: boolean;
  completable: boolean;
  expires_at?: string;
  orb_quantity?: number;
  icon_url?: string;
  hero_url?: string;
  colors?: {
    primary: string;
    secondary: string;
  };
}

export interface QuestProgressEvent {
  quest_id: string;
  quest_name: string;
  task_type: string;
  seconds_done: number;
  seconds_needed: number;
  percent: number;
  status_text: string;
  running: boolean;
  completed: boolean;
  error?: string;
}
