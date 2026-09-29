import React, { useCallback, useEffect, useState } from 'react';
import ISO6391 from 'iso-639-1';
import { useAuth } from '../context/AuthContext';
import { useI18n } from '../context/I18nContext';
import { discordTokenErrorMessage } from '../i18n';
import { getDiscordUser } from '../api/discordApi';
import type { DiscordUser } from '../types/discord';
import { Avatar } from '../components/common/Avatar';
import { Button } from '../components/common/Button';
import { IconDiscord, IconShield, IconRefresh } from '../components/icons';

export const AccountView: React.FC = () => {
  const { user, account } = useAuth();
  const { t, locale } = useI18n();
  const [discordUser, setDiscordUser] = useState<DiscordUser | null>(null);
  const [isLoading, setIsLoading] = useState(true);
  const [isRefreshing, setIsRefreshing] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchDiscord = useCallback(async (explicitRefresh = false) => {
    setIsRefreshing(explicitRefresh);
    setError(null);
    try {
      const data = await getDiscordUser(explicitRefresh);
      setDiscordUser(data);
    } catch (err: any) {
      setDiscordUser(null);
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to fetch Discord user profile');
    } finally {
      setIsLoading(false);
      setIsRefreshing(false);
    }
  }, [t]);

  useEffect(() => {
    void fetchDiscord();
  }, [fetchDiscord]);

  const standingLabel = account?.banned
    ? t('standingBanned')
    : (account?.account_standing ?? 0) === 0
    ? t('standingGood')
    : t('standingLimited');

  const standingClass = account?.banned
    ? 'standing-badge--banned'
    : (account?.account_standing ?? 0) === 0
    ? 'standing-badge--good'
    : 'standing-badge--limited';

  const formatCreationDate = (isoStr: string) => {
    try {
      const d = new Date(isoStr);
      return new Intl.DateTimeFormat(locale, {
        day: 'numeric',
        month: 'numeric',
        year: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
      }).format(d);
    } catch {
      return isoStr;
    }
  };

  const formatLanguageName = (raw?: string | null): string => {
    if (!raw) return 'English';
    const clean = raw.trim().replace('_', '-');
    const [langCode, regionCode] = clean.split('-');
    const primary = (langCode || '').toLowerCase();

    if (ISO6391.validate(primary)) {
      const name = ISO6391.getName(primary);
      if (regionCode) {
        return `${name} (${regionCode.toUpperCase()})`;
      }
      return name;
    }

    try {
      const displayName = new Intl.DisplayNames(['en'], { type: 'language' }).of(clean);
      if (displayName) return displayName;
    } catch {}

    return clean;
  };

  return (
    <div className="account-view">
      {/* ── Discord Account Card ── */}
      <section className="profile-hero-card discord-hero-card" aria-label={t('discordAccountTitle')}>
        <div
          className="discord-banner"
          style={{
            backgroundColor: discordUser?.banner_color || 'var(--bg-subtle)',
            backgroundImage: discordUser?.banner_url ? `url(${discordUser.banner_url})` : undefined,
          }}
        >
          <div className="discord-badge-pill">
            <IconDiscord size={18} />
            <span>Discord</span>
          </div>
        </div>

        <div className="discord-hero-body">
          <div className="discord-avatar-container">
            <Avatar
              src={discordUser?.avatar_url}
              name={discordUser?.global_name || discordUser?.username}
              size="lg"
              className="discord-avatar"
            />
          </div>

          <div className="discord-info-header">
            <div className="discord-names">
              <h1 className="discord-display-name">
                {isLoading ? t('loadingDetails') : (discordUser?.global_name || discordUser?.username || '—')}
              </h1>
              <p className="discord-username">
                {discordUser?.username ? `@${discordUser.username}` : ''}
              </p>
            </div>

            <Button
              variant="ghost"
              onClick={() => fetchDiscord(true)}
              disabled={isLoading || isRefreshing}
              title={t('retry')}
              aria-label={t('retry')}
            >
              <span className={isRefreshing ? 'refresh-icon is-spinning' : 'refresh-icon'}>
                <IconRefresh size={16} />
              </span>
            </Button>
          </div>

          {error ? (
            <div className="auth-error-banner" role="alert">
              {error}
            </div>
          ) : (
            <div className="account-meta-grid">
              <div className="meta-item">
                <span className="meta-label">{t('creationDateLabel')}</span>
                <span className="meta-value">
                  {discordUser?.created_at ? formatCreationDate(discordUser.created_at) : '—'}
                </span>
              </div>

              <div className="meta-item">
                <span className="meta-label">{t('userIdLabel')}</span>
                <span className="meta-value mono-text">
                  {discordUser?.id || '—'}
                </span>
              </div>
            </div>
          )}
        </div>
      </section>

      {/* ── Nixlabs Account Card ── */}
      {user && (
        <section className="nixlabs-profile-card" aria-label={t('nixlabsProfileTitle')}>
          <div className="card-header-row">
            <div className="card-header-icon tone-blue">
              <IconShield size={20} />
            </div>
            <div>
              <h2 className="card-title">{t('nixlabsProfileTitle')}</h2>
            </div>
          </div>

          <div className="nixlabs-profile-body">
            <div className="nixlabs-user-row">
              <Avatar src={user.avatar || user.avatarUrl} name={user.name} size="md" />
              <div className="nixlabs-user-names">
                <strong className="nixlabs-user-name">{user.name}</strong>
                <span className="nixlabs-user-handle">@{user.handle}</span>
              </div>
              <span className={`standing-badge ${standingClass}`}>
                {standingLabel}
              </span>
            </div>

            <div className="nixlabs-details-grid">
              {user.email && (
                <div className="meta-item">
                  <span className="meta-label">{t('emailLabel')}</span>
                  <span className="meta-value">{user.email}</span>
                </div>
              )}
              <div className="meta-item">
                <span className="meta-label">{t('roleLabel')}</span>
                <span className="meta-value">{user.role || 'MEMBER'}</span>
              </div>
              <div className="meta-item">
                <span className="meta-label">{t('languageLabel')}</span>
                <span className="meta-value">{formatLanguageName(user.language)}</span>
              </div>
            </div>
          </div>
        </section>
      )}
    </div>
  );
};
