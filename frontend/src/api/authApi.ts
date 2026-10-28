import { accountsRequest, apiRequest } from './client';
import type { SessionResponse, LoginResponse } from '../types/auth';

export async function checkSession(): Promise<SessionResponse> {
  try {
    const local = await apiRequest<SessionResponse>('/api/auth/session');
    if (local && local.authenticated) {
      return local;
    }
  } catch {}
  return accountsRequest<SessionResponse>('/api/session');
}

export async function loginInApp(identifier: string, password: string): Promise<LoginResponse> {
  try {
    const local = await apiRequest<any>('/api/auth/login', {
      method: 'POST',
      body: JSON.stringify({ identifier, password }),
    });
    if (local && (local.success || local.user)) {
      return { success: true, ...local };
    }
  } catch {}
  return accountsRequest<LoginResponse>('/api/login', {
    method: 'POST',
    body: JSON.stringify({ identifier, password }),
  });
}

export async function logout(): Promise<{ success: boolean }> {
  try {
    await apiRequest<{ success: boolean }>('/api/auth/logout', {
      method: 'POST',
    });
  } catch {}
  return accountsRequest<{ success: boolean }>('/api/logout', {
    method: 'POST',
  });
}

export async function getRegistrationUrl(): Promise<{ success: boolean; url: string }> {
  try {
    const res = await apiRequest<{ success: boolean; url: string }>('/api/auth/register-url');
    if (res && res.success && res.url) {
      return res;
    }
  } catch {}
  return {
    success: true,
    url: 'https://accounts.nixlabs.tech/login?mode=signup#signup',
  };
}

export async function openBrowserUrl(url: string): Promise<{ success: boolean }> {
  try {
    await apiRequest<{ success: boolean }>('/api/auth/open-browser', {
      method: 'POST',
      body: JSON.stringify({ url }),
    });
    return { success: true };
  } catch {
    window.open(url, '_blank');
    return { success: true };
  }
}
