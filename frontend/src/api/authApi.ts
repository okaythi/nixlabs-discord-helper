import { apiRequest } from './client';
import type { SessionResponse, LoginResponse } from '../types/auth';

export async function checkSession(): Promise<SessionResponse> {
  return apiRequest<SessionResponse>('/api/auth/session');
}

export async function loginInApp(identifier: string, password: string): Promise<LoginResponse> {
  return apiRequest<LoginResponse>('/api/auth/login', {
    method: 'POST',
    body: JSON.stringify({ identifier, password }),
  });
}

export async function logout(): Promise<{ success: boolean }> {
  return apiRequest<{ success: boolean }>('/api/auth/logout', {
    method: 'POST',
  });
}

export async function getRegistrationUrl(): Promise<{ success: boolean; url: string }> {
  return apiRequest<{ success: boolean; url: string }>('/api/auth/register-url', {
    method: 'POST',
  });
}

export async function openBrowserUrl(url: string): Promise<{ success: boolean }> {
  return apiRequest<{ success: boolean }>('/api/auth/open-browser', {
    method: 'POST',
    body: JSON.stringify({ url }),
  });
}
