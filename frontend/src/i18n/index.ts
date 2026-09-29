import { enUK, type Translations } from './locales/en-uk';
import { enUS } from './locales/en-us';
import { ptBR } from './locales/pt-br';
import { ptPT } from './locales/pt-pt';
import { ja } from './locales/ja';
import { frBE } from './locales/fr-be';

export type LocaleKey = 'en-uk' | 'en-us' | 'pt-br' | 'pt-pt' | 'ja' | 'fr-be';

export const locales: Record<LocaleKey, Translations> = {
  'en-uk': enUK,
  'en-us': enUS,
  'pt-br': ptBR,
  'pt-pt': ptPT,
  'ja': ja,
  'fr-be': frBE,
};

export function resolveLocale(lang?: string | null): LocaleKey {
  if (!lang) return 'en-uk';
  const clean = lang.toLowerCase().trim().replace('_', '-');
  if (clean === 'fr-be' || clean === 'fr') return 'fr-be';
  if (clean === 'en-uk' || clean === 'en-gb') return 'en-uk';
  if (clean === 'en-us' || clean === 'en') return 'en-us';
  if (clean === 'pt-br' || clean === 'pt') return 'pt-br';
  if (clean === 'pt-pt') return 'pt-pt';
  if (clean === 'ja' || clean === 'ja-jp') return 'ja';
  return 'en-uk';
}

export function formatString(template: string, vars: Record<string, string | number>): string {
  return template.replace(/\{(\w+)\}/g, (_, key) => {
    return vars[key] !== undefined ? String(vars[key]) : `{${key}}`;
  });
}

export const questTypeTranslationKeys: Record<string, keyof Translations> = {
  WATCH_VIDEO: 'questTypeWatchVideo',
  WATCH_VIDEO_ON_MOBILE: 'questTypeWatchVideoOnMobile',
  PLAY_ON_DESKTOP: 'questTypePlayOnDesktop',
  STREAM_ON_DESKTOP: 'questTypeStreamOnDesktop',
  PLAY_ACTIVITY: 'questTypePlayActivity',
};

export function formatQuestType(type: string, t: (key: keyof Translations) => string): string {
  const key = questTypeTranslationKeys[type];
  if (key) return t(key);
  return type;
}

export function discordTokenErrorMessage(error: unknown, t: (key: keyof Translations) => string): string | null {
  if (!(error instanceof Error)) return null;
  if (error.message === 'discord_token_missing') return t('discordTokenMissing');
  if (error.message === 'discord_token_invalid') return t('discordTokenInvalid');
  return null;
}
