import { apiRequest } from './client';
import type { Quest, QuestProgressEvent } from '../types/quests';

export async function getQuests(forceRefresh = false): Promise<{ quests: Quest[] }> {
  const url = forceRefresh ? '/api/quests?refresh=true' : '/api/quests';
  return apiRequest<{ quests: Quest[] }>(url);
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

export function subscribeQuestProgress(onEvent: (event: QuestProgressEvent) => void): () => void {
  const eventSource = new EventSource('/api/quests/events');
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
