import { apiRequest } from './client';
import type { Quest, QuestProgressEvent } from '../types/quests';

export interface QuestsResponse {
  quests: Quest[];
  total?: number;
  limit?: number;
  offset?: number;
  has_more?: boolean;
}

export async function getQuests(
  forceRefresh = false,
  limit?: number,
  offset?: number
): Promise<QuestsResponse> {
  const params = new URLSearchParams();
  if (forceRefresh) params.set('refresh', 'true');
  if (limit !== undefined && limit > 0) params.set('limit', String(limit));
  if (offset !== undefined && offset > 0) params.set('offset', String(offset));
  const query = params.toString();
  const url = query ? `/api/quests?${query}` : '/api/quests';
  return apiRequest<QuestsResponse>(url);
}

export async function getQuestProgress(): Promise<QuestProgressEvent> {
  return apiRequest<QuestProgressEvent>('/api/quests/progress');
}

export async function completeQuest(questId: string): Promise<{ success: boolean; message?: string }> {
  return apiRequest<{ success: boolean; message?: string }>('/api/quests/complete', {
    method: 'POST',
    body: JSON.stringify({ quest_id: questId }),
  });
}

export async function completeAllQuests(): Promise<{ success: boolean; message?: string }> {
  return apiRequest<{ success: boolean; message?: string }>('/api/quests/complete-all', {
    method: 'POST',
  });
}

export async function cancelQuest(): Promise<{ success: boolean }> {
  return apiRequest<{ success: boolean }>('/api/quests/cancel', {
    method: 'POST',
  });
}

import { getWorkerBaseUrl } from './client';

export function subscribeQuestProgress(onEvent: (event: QuestProgressEvent) => void): () => void {
  const eventSource = new EventSource(`${getWorkerBaseUrl()}/api/quests/progress`, { withCredentials: true });
  eventSource.onmessage = (e) => {
    try {
      const data = JSON.parse(e.data) as QuestProgressEvent;
      onEvent(data);
    } catch (err) {
      console.error('Failed to parse quest event:', err);
    }
  };
  eventSource.onerror = (e) => {
    // Keep alive or reconnect handled automatically by browser EventSource
  };

  return () => {
    eventSource.close();
  };
}
