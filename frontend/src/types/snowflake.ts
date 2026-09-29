export type SnowflakeLookup =
  | { kind: 'user'; id: string; username: string; global_name: string; avatar_url: string; banner_url: string; banner_color: string }
  | { kind: 'server'; id: string; name: string; icon_url: string; banner_url: string; description: string }
  | { kind: 'channel'; id: string; name: string; channel_type: number; guild_id: string; topic: string }
  | { kind: 'unknown'; id: string };
