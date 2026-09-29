import { apiRequest } from './client';
import type { DiscordUser } from '../types/discord';

export async function getDiscordUser(forceRefresh = false): Promise<DiscordUser> {
  return apiRequest<DiscordUser>(forceRefresh ? '/api/discord/user?refresh=true' : '/api/discord/user');
}
