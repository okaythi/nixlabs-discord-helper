import React, { createContext, useContext, useState, useEffect, useCallback } from 'react';
import type { SessionUser, NixlabsAccount } from '../types/auth';
import { checkSession, loginInApp as apiLogin, logout as apiLogout, getRegistrationUrl } from '../api/authApi';

interface AuthContextType {
  user: SessionUser | null;
  account: NixlabsAccount | null;
  isAuthenticated: boolean;
  isLoading: boolean;
  isDisconnected: boolean;
  checkConnection: () => Promise<boolean>;
  login: (identifier: string, pass: string) => Promise<void>;
  createAccountInBrowser: () => Promise<string>;
  logout: () => Promise<void>;
  refreshSession: () => Promise<void>;
}

const AuthContext = createContext<AuthContextType | null>(null);

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [user, setUser] = useState<SessionUser | null>(null);
  const [account, setAccount] = useState<NixlabsAccount | null>(null);
  const [isAuthenticated, setIsAuthenticated] = useState(false);
  const [isLoading, setIsLoading] = useState(true);
  const [isDisconnected, setIsDisconnected] = useState(false);

  const refreshSession = useCallback(async () => {
    try {
      const res = await checkSession();
      setIsDisconnected(false);
      if (res.authenticated && res.user && !res.account?.banned) {
        setUser(res.user);
        setAccount(res.account || null);
        setIsAuthenticated(true);
      } else {
        setUser(null);
        setAccount(null);
        setIsAuthenticated(false);
      }
    } catch {
      setIsDisconnected(true);
      setUser(null);
      setAccount(null);
      setIsAuthenticated(false);
    } finally {
      setIsLoading(false);
    }
  }, []);

  const checkConnection = useCallback(async (): Promise<boolean> => {
    try {
      const res = await checkSession();
      setIsDisconnected(false);
      if (res.authenticated && res.user && !res.account?.banned) {
        setUser(res.user);
        setAccount(res.account || null);
        setIsAuthenticated(true);
      }
      return true;
    } catch {
      setIsDisconnected(true);
      return false;
    }
  }, []);

  useEffect(() => {
    const handleOnline = () => { void checkConnection(); };
    const handleOffline = () => { setIsDisconnected(true); };
    window.addEventListener('online', handleOnline);
    window.addEventListener('offline', handleOffline);
    return () => {
      window.removeEventListener('online', handleOnline);
      window.removeEventListener('offline', handleOffline);
    };
  }, [checkConnection]);

  useEffect(() => {
    refreshSession();
  }, [refreshSession]);

  const login = async (identifier: string, pass: string) => {
    const res = await apiLogin(identifier, pass);
    if (!res.success) {
      throw new Error(res.error || 'Authentication failed');
    }
    await refreshSession();
  };

  const createAccountInBrowser = async (): Promise<string> => {
    const res = await getRegistrationUrl();
    if (!res.success || !res.url) {
      throw new Error('Failed to retrieve registration URL');
    }
    return res.url;
  };

  const logout = async () => {
    try {
      await apiLogout();
    } finally {
      setUser(null);
      setAccount(null);
      setIsAuthenticated(false);
    }
  };

  return (
    <AuthContext.Provider
      value={{
        user,
        account,
        isAuthenticated,
        isLoading,
        isDisconnected,
        checkConnection,
        login,
        createAccountInBrowser,
        logout,
        refreshSession,
      }}
    >
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth() {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used within AuthProvider');
  return ctx;
}
