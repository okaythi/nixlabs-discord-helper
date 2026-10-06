import React, { useState } from 'react';
import { useI18n } from '../../context/I18nContext';
import { Button } from '../common/Button';
import { IconDiscord, IconCheckCircle, IconExternal } from '../icons';
import { loginDiscord, submitDiscordMFA } from '../../api/discordApi';

interface DiscordLoginModalProps {
  isOpen: boolean;
  onClose: () => void;
  onSuccess: () => void;
}

export const DiscordLoginModal: React.FC<DiscordLoginModalProps> = ({
  isOpen,
  onClose,
  onSuccess,
}) => {
  const { t } = useI18n();

  const [login, setLogin] = useState('');
  const [password, setPassword] = useState('');

  // 2FA state
  const [mfaTicket, setMfaTicket] = useState<string | null>(null);
  const [mfaCode, setMfaCode] = useState('');

  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);

  if (!isOpen) return null;

  const handleCredentialsSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!login.trim() || !password) return;

    setError(null);
    setIsLoading(true);
    try {
      const res = await loginDiscord(login.trim(), password);
      if (res.captcha) {
        setError(t('discordCaptchaError'));
        return;
      }
      if (res.mfa && res.ticket) {
        setMfaTicket(res.ticket);
        return;
      }
      if (res.success || res.token_saved) {
        setSuccessMessage(t('discordConnectedSuccess'));
        setTimeout(() => {
          onSuccess();
          onClose();
        }, 800);
        return;
      }
      setError(res.error || t('discordLoginFailed'));
    } catch (err: any) {
      setError(err?.message || t('discordLoginFailed'));
    } finally {
      setIsLoading(false);
    }
  };

  const handleMfaSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!mfaTicket || !mfaCode.trim()) return;

    setError(null);
    setIsLoading(true);
    try {
      const res = await submitDiscordMFA(mfaTicket, mfaCode.trim());
      if (res.success || res.token_saved) {
        setSuccessMessage(t('discordConnectedSuccess'));
        setTimeout(() => {
          onSuccess();
          onClose();
        }, 800);
        return;
      }
      setError(res.error || t('discordLoginFailed'));
    } catch (err: any) {
      setError(err?.message || t('discordLoginFailed'));
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="modal-backdrop" onClick={onClose} role="dialog" aria-modal="true" aria-labelledby="discord-modal-title">
      <div className="modal-sheet discord-login-sheet" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <div className="modal-header-icon discord-tone">
            <IconDiscord size={24} />
          </div>
          <h2 id="discord-modal-title" className="modal-title">
            {t('connectDiscord')}
          </h2>
          <button
            type="button"
            className="modal-close-button"
            onClick={onClose}
            aria-label={t('close')}
          >
            ×
          </button>
        </div>

        {successMessage ? (
          <div className="discord-login-success">
            <IconCheckCircle size={40} className="success-icon" />
            <p>{successMessage}</p>
          </div>
        ) : (
          <div className="modal-body">
            {error && (
              <div className="auth-error-banner" role="alert">
                {error}
              </div>
            )}

            {mfaTicket ? (
              <form onSubmit={handleMfaSubmit} className="discord-auth-form">
                <p className="login-subtitle">{t('discordMfaPrompt')}</p>
                <div className="form-group">
                  <label htmlFor="discord-mfa-code">{t('discordMfaCode')}</label>
                  <input
                    id="discord-mfa-code"
                    type="text"
                    inputMode="numeric"
                    autoComplete="one-time-code"
                    maxLength={8}
                    className="text-input mono-text"
                    placeholder="123456"
                    value={mfaCode}
                    onChange={(e) => setMfaCode(e.target.value)}
                    autoFocus
                    required
                    disabled={isLoading}
                  />
                </div>
                <div className="form-actions">
                  <Button
                    variant="ghost"
                    type="button"
                    onClick={() => { setMfaTicket(null); setMfaCode(''); setError(null); }}
                    disabled={isLoading}
                  >
                    {t('back')}
                  </Button>
                  <Button variant="primary" type="submit" disabled={isLoading || !mfaCode.trim()}>
                    {isLoading ? t('discordSigningIn') : t('discordMfaSubmit')}
                  </Button>
                </div>
              </form>
            ) : (
              <form onSubmit={handleCredentialsSubmit} className="discord-auth-form">
                <div className="form-group">
                  <label htmlFor="discord-login">{t('discordEmailLabel')}</label>
                  <input
                    id="discord-login"
                    type="text"
                    autoComplete="username"
                    autoCapitalize="none"
                    spellCheck={false}
                    className="text-input"
                    placeholder={t('discordEmailLabel')}
                    value={login}
                    onChange={(e) => setLogin(e.target.value)}
                    autoFocus
                    required
                    disabled={isLoading}
                  />
                </div>
                <div className="form-group">
                  <label htmlFor="discord-password">{t('discordPasswordLabel')}</label>
                  <input
                    id="discord-password"
                    type="password"
                    autoComplete="current-password"
                    className="text-input"
                    placeholder={t('discordPasswordLabel')}
                    value={password}
                    onChange={(e) => setPassword(e.target.value)}
                    required
                    disabled={isLoading}
                  />
                </div>
                <div className="form-actions">
                  <Button variant="ghost" type="button" onClick={onClose} disabled={isLoading}>
                    {t('cancel')}
                  </Button>
                  <Button variant="primary" type="submit" disabled={isLoading || !login.trim() || !password}>
                    {isLoading ? t('discordSigningIn') : t('connectDiscord')}
                  </Button>
                </div>

                <div className="discord-modal-footer">
                  <a
                    href="https://myaccount.nixlabs.tech/apps"
                    target="_blank"
                    rel="noopener noreferrer"
                    className="myaccount-token-link"
                  >
                    <span>{t('manageInMyAccount')}</span>
                    <IconExternal size={14} />
                  </a>
                </div>
              </form>
            )}
          </div>
        )}
      </div>
    </div>
  );
};
