import { accountsRequest } from './client';
import type { SessionResponse, LoginResponse } from '../types/auth';

export async function checkSession(): Promise<SessionResponse> {
  return accountsRequest<SessionResponse>('/api/session');
}

export async function loginInApp(identifier: string, password: string): Promise<LoginResponse> {
  return accountsRequest<LoginResponse>('/api/login', {
    method: 'POST',
    body: JSON.stringify({ identifier, password }),
  });
}

export async function logout(): Promise<{ success: boolean }> {
  return accountsRequest<{ success: boolean }>('/api/logout', {
    method: 'POST',
  });
}

export async function getRegistrationUrl(): Promise<{ success: boolean; url: string }> {
  return {
    success: true,
    url: 'https://accounts.nixlabs.tech/login?mode=signup#signup',
  };
}

export async function openBrowserUrl(url: string): Promise<{ success: boolean }> {
  window.open(url, '_blank');
  return { success: true };
}
