import React, { createContext, useContext, useState, useEffect, useCallback } from 'react';
import { locales, resolveLocale, formatString, type LocaleKey } from '../i18n';
import type { Translations } from '../i18n/locales/en-uk';

interface I18nContextType {
  locale: LocaleKey;
  setLocale: (l: LocaleKey) => void;
  t: (key: keyof Translations, params?: Record<string, string | number>) => string;
}

const I18nContext = createContext<I18nContextType | null>(null);

export function I18nProvider({
  userLanguage,
  children
}: {
  userLanguage?: string;
  children: React.ReactNode;
}) {
  const [locale, setLocaleState] = useState<LocaleKey>(() => resolveLocale(userLanguage));

  const setLocale = useCallback((l: LocaleKey) => {
    setLocaleState(l);
    if (typeof localStorage !== 'undefined') {
      localStorage.setItem('nixlabs_language', l);
    }
  }, []);

  useEffect(() => {
    if (userLanguage) {
      setLocaleState(resolveLocale(userLanguage));
    }
  }, [userLanguage]);

  const t = useCallback((key: keyof Translations, params?: Record<string, string | number>): string => {
    const dict = locales[locale] || locales['en-uk'];
    const text = dict[key] || locales['en-uk'][key] || String(key);
    return params ? formatString(text, params) : text;
  }, [locale]);

  return (
    <I18nContext.Provider value={{ locale, setLocale, t }}>
      {children}
    </I18nContext.Provider>
  );
}

export function useI18n() {
  const ctx = useContext(I18nContext);
  if (!ctx) throw new Error('useI18n must be used within I18nProvider');
  return ctx;
}
