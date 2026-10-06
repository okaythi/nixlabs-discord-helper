import { accountsRequest, apiRequest } from './client';
import type { DiscordUser } from '../types/discord';

export interface DiscordLoginResult {
  success?: boolean;
  mfa?: boolean;
  ticket?: string;
  token_saved?: boolean;
  captcha?: boolean;
  error?: string;
}

export async function getDiscordUser(forceRefresh = false): Promise<DiscordUser> {
  return accountsRequest<DiscordUser>(
    forceRefresh
      ? '/api/third-party-auth/discord-helper/bot-profile?refresh=true'
      : '/api/third-party-auth/discord-helper/bot-profile'
  );
}

export async function loginDiscord(login: string, password: string): Promise<DiscordLoginResult> {
  return apiRequest<DiscordLoginResult>('/api/auth/discord/login', {
    method: 'POST',
    body: JSON.stringify({ login, password }),
  });
}

export async function submitDiscordMFA(ticket: string, code: string): Promise<DiscordLoginResult> {
  return apiRequest<DiscordLoginResult>('/api/auth/discord/mfa', {
    method: 'POST',
    body: JSON.stringify({ ticket, code }),
  });
}

export async function saveDiscordToken(token: string): Promise<{ status: string }> {
  return accountsRequest<{ status: string }>('/api/third-party-auth/discord-helper/token', {
    method: 'PUT',
    body: JSON.stringify({ token }),
  });
}

export async function disconnectDiscord(): Promise<{ status: string }> {
  return accountsRequest<{ status: string }>('/api/third-party-auth/discord-helper/token', {
    method: 'DELETE',
  });
}

