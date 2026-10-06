import { accountsRequest } from './client';
import type { SnowflakeLookup } from '../types/snowflake';

export function lookupSnowflake(id: string): Promise<SnowflakeLookup> {
  return accountsRequest<SnowflakeLookup>(`/api/third-party-auth/discord-helper/snowflakes/${id}`);
}
