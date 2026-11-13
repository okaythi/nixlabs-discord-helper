import { apiRequest, getWorkerBaseUrl } from './client';
import type { Translations } from '../i18n/locales/en-uk';

export type UpdatePhase = 'idle' | 'available' | 'downloading' | 'installing' | 'completed' | 'error';

export interface UpdateStatus {
  available: boolean;
  current_version: string;
  latest_version?: string;
  phase: UpdatePhase;
  percent: number;
  download_percent: number;
  subtitle: string;
  dismissed: boolean;
  storage_type?: 'ssd' | 'hdd' | string;
  error?: string;
}

export function isDesktopBackend(): boolean {
  if (typeof window === 'undefined') return false;
  const { protocol, hostname } = window.location;
  return protocol === 'http:' && (hostname === '127.0.0.1' || hostname === 'localhost');
}

export function isUpdateModalVisible(status: UpdateStatus | null): boolean {
  return Boolean(status && status.available && !status.dismissed);
}

export function computeUpdateSubtitle(
  phase: UpdatePhase,
  downloadPercent: number,
  t: (key: keyof Translations, vars?: Record<string, string | number>) => string
): string {
  if (phase === 'downloading') {
    const clamped = Math.min(100, Math.max(0, Math.round(downloadPercent)));
    return t('updateDownloading', { percent: clamped });
  }
  if (phase === 'installing' || phase === 'completed') {
    return t('updateInstalling');
  }
  return '';
}

export async function getUpdateStatus(forceCheck = false): Promise<UpdateStatus | null> {
  if (!isDesktopBackend()) return null;
  const query = forceCheck ? '?check=true' : '';
  return apiRequest<UpdateStatus>(`/api/update/status${query}`);
}

export async function startUpdate(): Promise<UpdateStatus> {
  return apiRequest<UpdateStatus>('/api/update/start', {
    method: 'POST',
  });
}

export async function dismissUpdate(): Promise<UpdateStatus> {
  return apiRequest<UpdateStatus>('/api/update/dismiss', {
    method: 'POST',
  });
}

export function subscribeUpdateEvents(onEvent: (status: UpdateStatus) => void): () => void {
  if (!isDesktopBackend() || typeof EventSource === 'undefined') {
    return () => {};
  }
  const eventSource = new EventSource(`${getWorkerBaseUrl()}/api/update/events`, {
    withCredentials: true,
  });
  eventSource.onmessage = (e) => {
    try {
      const data = JSON.parse(e.data) as UpdateStatus;
      onEvent(data);
    } catch {
      // Ignore malformed SSE payloads
    }
  };
  eventSource.onerror = () => {
    // Reconnect is managed automatically by EventSource while mounted
  };
  return () => {
    eventSource.close();
  };
}
