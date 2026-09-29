export interface DiscordUser {
  id: string;
  username: string;
  global_name?: string;
  avatar?: string;
  avatar_url?: string;
  banner?: string;
  banner_url?: string;
  banner_color?: string;
  accent_color?: number;
  created_at: string;
  created_at_timestamp: number;
}
