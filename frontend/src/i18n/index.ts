import { enUK, type Translations } from './locales/en-uk';
import { enUS } from './locales/en-us';
import { ptBR } from './locales/pt-br';
import { ptPT } from './locales/pt-pt';
import { ja } from './locales/ja';

export type LocaleKey = 'en-uk' | 'en-us' | 'pt-br' | 'pt-pt' | 'ja';

export const locales: Record<LocaleKey, Translations> = {
  'en-uk': enUK,
  'en-us': enUS,
  'pt-br': ptBR,
  'pt-pt': ptPT,
  'ja': ja,
};

export function resolveLocale(lang?: string | null): LocaleKey {
  if (!lang) return 'en-uk';
  const clean = lang.toLowerCase().trim().replace('_', '-');
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
