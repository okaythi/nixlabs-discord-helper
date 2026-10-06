import { accountsRequest } from './client';
import type { DiscordUser } from '../types/discord';

export async function getDiscordUser(forceRefresh = false): Promise<DiscordUser> {
  return accountsRequest<DiscordUser>(
    forceRefresh
      ? '/api/third-party-auth/discord-helper/bot-profile?refresh=true'
      : '/api/third-party-auth/discord-helper/bot-profile'
  );
}
