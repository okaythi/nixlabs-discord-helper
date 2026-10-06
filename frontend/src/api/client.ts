const ACCOUNTS_BASE_URL = 'https://accounts.nixlabs.tech';

export async function accountsRequest<T>(endpoint: string, options: RequestInit = {}): Promise<T> {
  const url = `${ACCOUNTS_BASE_URL}${endpoint}`;
  const headers = new Headers(options.headers || {});
  if (options.body && !(options.body instanceof FormData)) {
    headers.set('Content-Type', 'application/json');
  }

  let response: Response;
  try {
    response = await fetch(url, {
      ...options,
      headers,
      credentials: 'include',
    });
  } catch {
    throw new Error('Could not reach accounts.nixlabs.tech. Please check your connection.');
  }

  if (!response.ok) {
    const errorData = await response.json().catch(() => ({}));
    if (response.status === 404 || errorData.status === 'missing') {
      const err: any = new Error('discord_token_missing');
      err.status = 404;
      err.data = errorData;
      throw err;
    }
    if (response.status === 422 || errorData.status === 'invalid') {
      const err: any = new Error('discord_token_invalid');
      err.status = 422;
      err.data = errorData;
      throw err;
    }
    const err: any = new Error(errorData.error || `Request failed with status ${response.status}`);
    err.status = response.status;
    err.data = errorData;
    throw err;
  }

  return response.json() as Promise<T>;
}

export function getWorkerBaseUrl(): string {
  return typeof window !== 'undefined' && window.location.hostname === 'helper.nixlabs.tech'
    ? 'https://api-helper.nixlabs.tech'
    : '';
}

export async function apiRequest<T>(endpoint: string, options: RequestInit = {}): Promise<T> {
  const url = `${getWorkerBaseUrl()}${endpoint}`;
  const headers = new Headers(options.headers || {});
  if (options.body && !(options.body instanceof FormData)) {
    headers.set('Content-Type', 'application/json');
  }

  let response: Response;
  try {
    response = await fetch(url, {
      ...options,
      headers,
      credentials: 'include',
    });
  } catch {
    throw new Error('Could not reach the Discord Helper service. Please try again.');
  }

  if (!response.ok) {
    const errorData = await response.json().catch(() => ({}));
    if (response.status === 409 || errorData.captcha || (typeof errorData.error === 'string' && errorData.error.toLowerCase().includes('captcha'))) {
      return { captcha: true, error: 'captcha_required' } as T;
    }
    throw new Error(errorData.error || `Request failed with status ${response.status}`);
  }

  return response.json() as Promise<T>;
}
