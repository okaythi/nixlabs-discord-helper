export const REFRESH_INTERVAL_MS = 12 * 60 * 60 * 1000;

export interface RefreshCache<T> {
  checkedAt: number;
  value: T | null;
}

export function cacheKey(kind: string, accountId: string): string {
  return `nixlabs-discord-helper:${kind}:${accountId}`;
}

export function readRefreshCache<T>(key: string): RefreshCache<T> {
  try {
    const raw = window.localStorage.getItem(key);
    if (raw) {
      const parsed = JSON.parse(raw) as RefreshCache<T>;
      if (typeof parsed.checkedAt === 'number' &&
          Number.isFinite(parsed.checkedAt) &&
          parsed.checkedAt > 0 &&
          parsed.checkedAt <= Date.now() &&
          'value' in parsed) {
        return parsed;
      }
    }
  } catch {
    // Storage can be unavailable or contain an older, invalid value.
  }
  return { checkedAt: 0, value: null };
}

export function writeRefreshCache<T>(key: string, cache: RefreshCache<T>): void {
  try {
    window.localStorage.setItem(key, JSON.stringify(cache));
  } catch {
    // The view still works if persistent storage is unavailable.
  }
}
