import { apiRequest } from './client';
import type { DiscordUser } from '../types/discord';

export async function getDiscordUser(): Promise<DiscordUser> {
  return apiRequest<DiscordUser>('/api/discord/user');
}
