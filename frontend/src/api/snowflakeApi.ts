import { apiRequest } from './client';
import type { SnowflakeLookup } from '../types/snowflake';

export function lookupSnowflake(id: string): Promise<SnowflakeLookup> {
  return apiRequest<SnowflakeLookup>(`/api/discord/snowflake/${id}`);
}
