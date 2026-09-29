export interface SessionUser {
  id?: string;
  name: string;
  handle: string;
  email?: string;
  recovery_email?: string;
  avatar?: string;
  avatarUrl?: string;
  role?: string;
  role_flags?: number;
  roles?: string[];
  achievement_flags?: number;
  achievements?: string[];
  language?: string;
  theme?: string;
}

export interface NixlabsAccount {
  account_standing: number;
  banned: boolean;
}

export interface SessionResponse {
  authenticated: boolean;
  user?: SessionUser;
  account?: NixlabsAccount;
  exp?: number;
  error?: string;
}

export interface LoginResponse {
  success: boolean;
  user?: SessionUser;
  account?: NixlabsAccount;
  error?: string;
  migrationRequired?: boolean;
  token?: string;
}
