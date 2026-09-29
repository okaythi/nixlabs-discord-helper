import React, { useEffect, useState } from 'react';
import { useI18n } from '../context/I18nContext';
import { Button } from '../components/common/Button';
import { IconCheck, IconCopy, IconSnowflake } from '../components/icons';
import { Avatar } from '../components/common/Avatar';
import { lookupSnowflake } from '../api/snowflakeApi';
import type { SnowflakeLookup } from '../types/snowflake';

const DISCORD_EPOCH = 1420070400000n;
const MAX_SNOWFLAKE = (1n << 64n) - 1n;

type SnowflakeDetails = {
  id: string;
  timestamp: number;
  timestampBits: string;
  workerId: string;
  processId: string;
  increment: string;
};

function decodeSnowflake(input: string): SnowflakeDetails | null {
  const id = input.trim();
  if (!/^\d+$/.test(id)) return null;
  const value = BigInt(id);
  if (value === 0n || value > MAX_SNOWFLAKE) return null;
  const timestampBits = value >> 22n;
  const timestamp = Number(timestampBits + DISCORD_EPOCH);
  if (timestamp > Date.now()) return null;
  return {
    id: value.toString(),
    timestamp,
    timestampBits: timestampBits.toString(),
    workerId: ((value >> 17n) & 31n).toString(),
    processId: ((value >> 12n) & 31n).toString(),
    increment: (value & 4095n).toString(),
  };
}

export const SnowflakesView: React.FC = () => {
  const { t, locale } = useI18n();
  const [input, setInput] = useState('');
  const [now, setNow] = useState(Date.now());
  const [copied, setCopied] = useState<string | null>(null);
  const [lookup, setLookup] = useState<SnowflakeLookup | null>(null);
  const [lookupState, setLookupState] = useState<'idle' | 'loading' | 'unavailable'>('idle');
  const details = decodeSnowflake(input);
  const invalid = input.trim().length > 0 && !details;

  useEffect(() => {
    setLookup(null);
    if (!details) {
      setLookupState('idle');
      return;
    }
    let active = true;
    setLookupState('loading');
    const timer = window.setTimeout(() => {
      lookupSnowflake(details.id).then((result) => {
        if (!active) return;
        setLookup(result);
        setLookupState('idle');
      }).catch(() => {
        if (active) setLookupState('unavailable');
      });
    }, 350);
    return () => { active = false; window.clearTimeout(timer); };
  }, [details?.id]);

  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 60_000);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    if (!copied) return;
    const timer = window.setTimeout(() => setCopied(null), 2000);
    return () => window.clearTimeout(timer);
  }, [copied]);

  const copy = async (key: string, value: string) => {
    try {
      await navigator.clipboard.writeText(value);
      setCopied(key);
    } catch {
      setCopied(null);
    }
  };

  const age = details ? Math.max(0, now - details.timestamp) : 0;
  const relativeFormatter = new Intl.RelativeTimeFormat(locale, { numeric: 'auto' });
  const ageParts: { unit: Intl.RelativeTimeFormatUnit; value: number }[] = [
    { unit: 'year', value: Math.floor(age / 31_557_600_000) },
    { unit: 'day', value: Math.floor(age / 86_400_000) },
    { unit: 'hour', value: Math.floor(age / 3_600_000) },
    { unit: 'minute', value: Math.floor(age / 60_000) },
    { unit: 'second', value: Math.floor(age / 1000) },
  ];
  const firstAgePart = ageParts.find(({ value }) => value > 0);
  const relativeAge = firstAgePart
    ? relativeFormatter.format(-firstAgePart.value, firstAgePart.unit)
    : relativeFormatter.format(0, 'second');

  const rows = details ? [
    { key: 'id', label: t('snowflakeRawId'), value: details.id },
    { key: 'timestamp', label: t('snowflakeUnixMs'), value: String(details.timestamp) },
    { key: 'timestampBits', label: t('snowflakeTimestampBits'), value: details.timestampBits },
    { key: 'workerId', label: t('snowflakeWorkerId'), value: details.workerId },
    { key: 'processId', label: t('snowflakeProcessId'), value: details.processId },
    { key: 'increment', label: t('snowflakeIncrement'), value: details.increment },
  ] : [];

  return (
    <div className="snowflakes-view">
      <div className="view-header-row">
        <div>
          <h1 className="view-title">{t('tabSnowflakes')}</h1>
          <p className="view-subtitle">{t('snowflakesSubtitle')}</p>
        </div>
      </div>

      <section className="nixlabs-profile-card" aria-label={t('snowflakeInputLabel')}>
        <div className="card-header-row">
          <div className="card-header-icon tone-sky"><IconSnowflake size={20} /></div>
          <div><h2 className="card-title">{t('snowflakeInputTitle')}</h2></div>
        </div>
        <div className="form-group">
          <label className="meta-label" htmlFor="snowflake-id">{t('snowflakeInputLabel')}</label>
          <input
            id="snowflake-id"
            className="text-input mono-text"
            type="text"
            inputMode="numeric"
            autoComplete="off"
            spellCheck={false}
            placeholder={t('snowflakePlaceholder')}
            value={input}
            aria-invalid={invalid}
            aria-describedby="snowflake-help"
            onChange={(event) => { setInput(event.target.value); setCopied(null); }}
          />
          <p id="snowflake-help" className={invalid ? 'snowflake-help is-error' : 'snowflake-help'} role={invalid ? 'alert' : undefined}>
            {invalid ? t('snowflakeInvalid') : t('snowflakeInputHint')}
          </p>
        </div>
      </section>

      {details && (
        <>
          <section className="profile-hero-card snowflake-identity-card" aria-label={t('snowflakeIdentityTitle')}>
            <div className="discord-banner snowflake-identity-banner" style={lookup?.kind === 'user' ? { backgroundColor: lookup.banner_color || 'var(--bg-subtle)', backgroundImage: lookup.banner_url ? `url(${lookup.banner_url})` : undefined } : lookup?.kind === 'server' ? { backgroundImage: lookup.banner_url ? `url(${lookup.banner_url})` : undefined } : undefined}>
              <span className="discord-badge-pill">{lookup?.kind === 'user' ? t('snowflakeUser') : lookup?.kind === 'server' ? t('snowflakeServer') : lookup?.kind === 'channel' ? t('snowflakeChannel') : t('snowflakeIdentityTitle')}</span>
            </div>
            <div className="discord-hero-body snowflake-identity-body">
              {(lookup?.kind === 'user' || lookup?.kind === 'server') && (
                <div className="discord-avatar-container">
                  <Avatar src={lookup.kind === 'user' ? lookup.avatar_url : lookup.icon_url} name={lookup.kind === 'user' ? (lookup.global_name || lookup.username) : lookup.name} size="lg" className="discord-avatar" />
                </div>
              )}
              <h2 className="discord-display-name">
                {lookup?.kind === 'user' ? (lookup.global_name || lookup.username) : lookup?.kind === 'server' ? lookup.name : lookup?.kind === 'channel' ? `#${lookup.name}` : lookupState === 'loading' ? t('loadingDetails') : t('snowflakeUnresolved')}
              </h2>
              {lookup?.kind === 'user' && <p className="discord-username">@{lookup.username}</p>}
              {lookup?.kind === 'server' && lookup.description && <p className="discord-username">{lookup.description}</p>}
              {lookup?.kind === 'channel' && lookup.topic && <p className="discord-username">{lookup.topic}</p>}
              {lookup?.kind === 'channel' && lookup.guild_id && <p className="discord-username mono-text">{t('snowflakeServerId')}: {lookup.guild_id}</p>}
              {(lookup?.kind === 'unknown' || lookupState === 'unavailable') && <p className="discord-username">{lookupState === 'unavailable' ? t('snowflakeLookupUnavailable') : t('snowflakeLookupUnknown')}</p>}
            </div>
          </section>
          <section className="nixlabs-profile-card" aria-label={t('snowflakeCreatedAt')}>
            <div className="card-header-row">
              <div className="card-header-icon tone-blue"><IconSnowflake size={20} /></div>
              <div><h2 className="card-title">{t('snowflakeCreatedAt')}</h2></div>
            </div>
            <div className="snowflake-main-result">
              <div>
                <div className="snowflake-date">{new Intl.DateTimeFormat(locale, { dateStyle: 'full', timeStyle: 'medium' }).format(details.timestamp)}</div>
                <div className="snowflake-age">{relativeAge}</div>
              </div>
              <Button variant="ghost" onClick={() => copy('iso', new Date(details.timestamp).toISOString())} aria-label={copied === 'iso' ? t('snowflakeCopied') : t('snowflakeCopyDate')} title={copied === 'iso' ? t('snowflakeCopied') : t('snowflakeCopyDate')}>
                {copied === 'iso' ? <IconCheck size={16} /> : <IconCopy size={16} />}
              </Button>
            </div>
          </section>

          <section className="nixlabs-profile-card" aria-label={t('snowflakeDetailsTitle')}>
            <div className="card-header-row">
              <div className="card-header-icon tone-purple"><IconSnowflake size={20} /></div>
              <div><h2 className="card-title">{t('snowflakeDetailsTitle')}</h2></div>
            </div>
            <div className="snowflake-details-grid">
              {rows.map(({ key, label, value }) => (
                <div className="snowflake-detail" key={key}>
                  <div className="meta-item">
                    <span className="meta-label">{label}</span>
                    <span className="meta-value mono-text">{value}</span>
                  </div>
                  <Button variant="ghost" onClick={() => copy(key, value)} aria-label={`${copied === key ? t('snowflakeCopied') : t('snowflakeCopy')} ${label}`} title={copied === key ? t('snowflakeCopied') : t('snowflakeCopy')}>
                    {copied === key ? <IconCheck size={16} /> : <IconCopy size={16} />}
                  </Button>
                </div>
              ))}
            </div>
          </section>
        </>
      )}
    </div>
  );
};
