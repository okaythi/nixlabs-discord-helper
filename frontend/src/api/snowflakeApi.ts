import { accountsRequest, apiRequest } from './client';
import type { SnowflakeLookup } from '../types/snowflake';

export async function lookupSnowflake(id: string): Promise<SnowflakeLookup> {
  try {
    return await apiRequest<SnowflakeLookup>(`/api/discord/snowflake/${id}`);
  } catch {
    return accountsRequest<SnowflakeLookup>(`/api/third-party-auth/discord-helper/snowflakes/${id}`);
  }
}
