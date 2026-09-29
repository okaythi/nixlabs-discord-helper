import React, { useState } from 'react';
import { useAuth } from '../context/AuthContext';
import { useI18n } from '../context/I18nContext';
import { Button } from '../components/common/Button';
import { openBrowserUrl } from '../api/authApi';

type LoginMode = 'card' | 'in_app_form' | 'awaiting_browser';

export const LoginView: React.FC = () => {
  const { login, createAccountInBrowser } = useAuth();
  const { t } = useI18n();

  const [mode, setMode] = useState<LoginMode>('card');
  const [identifier, setIdentifier] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [registrationUrl, setRegistrationUrl] = useState<string | null>(null);

  const handleInAppSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!identifier.trim() || !password) return;

    setError(null);
    setIsSubmitting(true);
    try {
      await login(identifier.trim(), password);
    } catch (err: any) {
      setError(err?.message || t('loginFailed'));
    } finally {
      setIsSubmitting(false);
    }
  };

  const handleCreateAccountClick = async () => {
    setError(null);
    setIsSubmitting(true);
    try {
      const url = await createAccountInBrowser();
      setRegistrationUrl(url);
      // Open default browser via window.open (WebKitGTK runner handles via create signal)
      window.open(url, '_blank');
      setMode('awaiting_browser');
    } catch (err: any) {
      setError(err?.message || t('networkError'));
    } finally {
      setIsSubmitting(false);
    }
  };

  return (
    <div className="auth-screen">
      <div className="auth-card">
        <span className="wordmark">{t('brandName')}</span>
        <h1>{t('yourAccount')}</h1>

        {error && (
          <div className="auth-error-banner" role="alert">
            {error}
          </div>
        )}

        {mode === 'card' && (
          <div className="auth-actions">
            <Button
              variant="primary"
              onClick={() => {
                setError(null);
                setMode('in_app_form');
              }}
            >
              {t('signIn')}
            </Button>
            <Button
              variant="secondary"
              onClick={handleCreateAccountClick}
              disabled={isSubmitting}
            >
              {isSubmitting ? t('opening') : t('createAccount')}
            </Button>
          </div>
        )}

        {mode === 'in_app_form' && (
          <form className="in-app-login-form" onSubmit={handleInAppSubmit}>
            <p className="login-subtitle">{t('signInPrompt')}</p>

            <div className="form-group">
              <input
                type="text"
                className="text-input"
                placeholder={t('identifierPlaceholder')}
                value={identifier}
                onChange={(e) => setIdentifier(e.target.value)}
                autoFocus
                required
                disabled={isSubmitting}
              />
            </div>

            <div className="form-group">
              <input
                type="password"
                className="text-input"
                placeholder={t('passwordPlaceholder')}
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                required
                disabled={isSubmitting}
              />
            </div>

            <div className="auth-actions">
              <Button
                type="submit"
                variant="primary"
                disabled={isSubmitting || !identifier.trim() || !password}
              >
                {isSubmitting ? t('signingIn') : t('signIn')}
              </Button>
              <Button
                type="button"
                variant="ghost"
                onClick={() => {
                  setError(null);
                  setMode('card');
                }}
                disabled={isSubmitting}
              >
                {t('back')}
              </Button>
            </div>
          </form>
        )}

        {mode === 'awaiting_browser' && (
          <div className="awaiting-browser-box">
            <div className="pulse-indicator" aria-hidden="true" />
            <h3>{t('browserHandoffWaiting')}</h3>
            <p>{t('browserHandoffNote')}</p>
            {registrationUrl && (
              <a
                href={registrationUrl}
                target="_blank"
                rel="noopener noreferrer"
                className="browser-retry-link"
                onClick={(e) => {
                  e.preventDefault();
                  window.open(registrationUrl, '_blank');
                  openBrowserUrl(registrationUrl).catch(() => {});
                }}
              >
                {t('reopenBrowser')}
              </a>
            )}
            <Button
              variant="ghost"
              onClick={() => {
                setRegistrationUrl(null);
                setMode('in_app_form');
              }}
            >
              {t('signIn')}
            </Button>
          </div>
        )}
      </div>
    </div>
  );
};
