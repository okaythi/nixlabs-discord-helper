import React, { useState, useEffect, useRef } from 'react';
import { useI18n } from '../../context/I18nContext';
import { Button } from '../common/Button';
import { IconDiscord, IconCheckCircle, IconExternal } from '../icons';
import { loginDiscord, submitDiscordMFA, saveDiscordToken } from '../../api/discordApi';

interface DiscordLoginModalProps {
  isOpen: boolean;
  onClose: () => void;
  onSuccess: () => void;
}

interface CaptchaChallenge {
  sitekey: string;
  rqdata: string;
  rqtoken: string;
}

const HCAPTCHA_SCRIPT_ID = 'hcaptcha-sdk-script';

function loadHcaptchaScript(): Promise<void> {
  return new Promise((resolve, reject) => {
    if (typeof window === 'undefined') return resolve();
    if ((window as any).hcaptcha && typeof (window as any).hcaptcha.render === 'function') {
      return resolve();
    }

    const existing = document.getElementById(HCAPTCHA_SCRIPT_ID) as HTMLScriptElement | null;
    if (existing) {
      if ((window as any).hcaptcha && typeof (window as any).hcaptcha.render === 'function') {
        return resolve();
      }
      const checkInterval = setInterval(() => {
        if ((window as any).hcaptcha && typeof (window as any).hcaptcha.render === 'function') {
          clearInterval(checkInterval);
          resolve();
        }
      }, 50);
      existing.addEventListener('error', (err) => {
        clearInterval(checkInterval);
        reject(err);
      });
      return;
    }

    (window as any).onHcaptchaLoaded = () => {
      resolve();
    };

    const script = document.createElement('script');
    script.id = HCAPTCHA_SCRIPT_ID;
    script.src = 'https://js.hcaptcha.com/1/api.js?onload=onHcaptchaLoaded&render=explicit';
    script.async = true;
    script.defer = true;
    script.onerror = (e) => reject(e);
    document.head.appendChild(script);

    setTimeout(() => {
      if ((window as any).hcaptcha && typeof (window as any).hcaptcha.render === 'function') {
        resolve();
      }
    }, 3000);
  });
}

export const DiscordLoginModal: React.FC<DiscordLoginModalProps> = ({
  isOpen,
  onClose,
  onSuccess,
}) => {
  const { t } = useI18n();

  const [activeTab, setActiveTab] = useState<'credentials' | 'token'>('credentials');

  // Credentials state
  const [login, setLogin] = useState('');
  const [password, setPassword] = useState('');

  // Captcha state
  const [captchaChallenge, setCaptchaChallenge] = useState<CaptchaChallenge | null>(null);
  const captchaContainerRef = useRef<HTMLDivElement>(null);
  const widgetIdRef = useRef<string | null>(null);

  // 2FA state
  const [mfaTicket, setMfaTicket] = useState<string | null>(null);
  const [mfaCode, setMfaCode] = useState('');

  // Token state
  const [tokenInput, setTokenInput] = useState('');

  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);

  const handleClose = () => {
    setCaptchaChallenge(null);
    setMfaTicket(null);
    setMfaCode('');
    setError(null);
    setSuccessMessage(null);
    onClose();
  };

  useEffect(() => {
    let mounted = true;
    if (!captchaChallenge || !captchaContainerRef.current) return;

    loadHcaptchaScript()
      .then(() => {
        if (!mounted || !captchaContainerRef.current) return;
        const hcaptcha = (window as any).hcaptcha;
        if (!hcaptcha || typeof hcaptcha.render !== 'function') return;

        if (widgetIdRef.current !== null && hcaptcha.remove) {
          try {
            hcaptcha.remove(widgetIdRef.current);
          } catch (_) {}
          widgetIdRef.current = null;
        }
        captchaContainerRef.current.innerHTML = '';

        try {
          const id = hcaptcha.render(captchaContainerRef.current, {
            sitekey: captchaChallenge.sitekey,
            theme: 'dark',
            size: 'normal',
            ...(captchaChallenge.rqdata ? { rqdata: captchaChallenge.rqdata } : {}),
            callback: (token: string) => {
              if (mounted) {
                handleCaptchaSolved(token);
              }
            },
            'error-callback': (err: any) => {
              console.error('hCaptcha widget error:', err);
              if (mounted) {
                setError(t('discordCaptchaError'));
              }
            },
          });
          widgetIdRef.current = id;
        } catch (renderErr) {
          console.error('Failed to render hCaptcha:', renderErr);
        }
      })
      .catch((err) => {
        console.error('Failed to load hCaptcha script:', err);
        if (mounted) {
          setError(t('discordCaptchaError'));
        }
      });

    return () => {
      mounted = false;
      const hcaptcha = (window as any).hcaptcha;
      if (widgetIdRef.current !== null && hcaptcha?.remove) {
        try {
          hcaptcha.remove(widgetIdRef.current);
        } catch (_) {}
        widgetIdRef.current = null;
      }
    };
  }, [captchaChallenge]);

  if (!isOpen) return null;

  const handleCredentialsSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!login.trim() || !password) return;

    setError(null);
    setIsLoading(true);
    try {
      const res = await loginDiscord(login.trim(), password);
      if (res.captcha) {
        const sitekey = res.captcha_sitekey || 'a9b5fb07-92ff-493f-86fe-352a2803b3df';
        setCaptchaChallenge({
          sitekey,
          rqdata: res.captcha_rqdata || '',
          rqtoken: res.captcha_rqtoken || '',
        });
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
          handleClose();
        }, 800);
        return;
      }
      setError(res.error || t('discordLoginFailed'));
    } catch (err: any) {
      if (err?.message && err.message.toLowerCase().includes('captcha')) {
        setError(t('discordCaptchaError'));
      } else {
        setError(err?.message || t('discordLoginFailed'));
      }
    } finally {
      setIsLoading(false);
    }
  };

  const handleCaptchaSolved = async (captchaToken: string) => {
    if (!captchaChallenge) return;
    setError(null);
    setIsLoading(true);
    try {
      const res = await loginDiscord(
        login.trim(),
        password,
        captchaToken,
        captchaChallenge.rqtoken
      );
      if (res.mfa && res.ticket) {
        setCaptchaChallenge(null);
        setMfaTicket(res.ticket);
        return;
      }
      if (res.success || res.token_saved) {
        setCaptchaChallenge(null);
        setSuccessMessage(t('discordConnectedSuccess'));
        setTimeout(() => {
          onSuccess();
          handleClose();
        }, 800);
        return;
      }
      if (res.captcha) {
        setCaptchaChallenge({
          sitekey: res.captcha_sitekey || captchaChallenge.sitekey,
          rqdata: res.captcha_rqdata || '',
          rqtoken: res.captcha_rqtoken || '',
        });
        const hcaptcha = (window as any).hcaptcha;
        if (widgetIdRef.current !== null && hcaptcha?.reset) {
          hcaptcha.reset(widgetIdRef.current);
        }
        return;
      }
      setError(res.error || t('discordLoginFailed'));
      const hcaptcha = (window as any).hcaptcha;
      if (widgetIdRef.current !== null && hcaptcha?.reset) {
        hcaptcha.reset(widgetIdRef.current);
      }
    } catch (err: any) {
      setError(err?.message || t('discordLoginFailed'));
      const hcaptcha = (window as any).hcaptcha;
      if (widgetIdRef.current !== null && hcaptcha?.reset) {
        hcaptcha.reset(widgetIdRef.current);
      }
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
          handleClose();
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

  const handleTokenSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!tokenInput.trim()) return;

    setError(null);
    setIsLoading(true);
    try {
      await saveDiscordToken(tokenInput.trim());
      setSuccessMessage(t('discordConnectedSuccess'));
      setTimeout(() => {
        onSuccess();
        handleClose();
      }, 800);
    } catch (err: any) {
      if (err?.status === 422 || err?.message === 'discord_token_invalid') {
        setError(t('discordInvalidToken'));
      } else {
        setError(err?.message || t('discordInvalidToken'));
      }
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="modal-backdrop" onClick={handleClose} role="dialog" aria-modal="true" aria-labelledby="discord-modal-title">
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
            onClick={handleClose}
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
            {!mfaTicket && !captchaChallenge && (
              <div className="login-tab-bar" role="tablist">
                <button
                  type="button"
                  role="tab"
                  aria-selected={activeTab === 'credentials'}
                  className={`login-tab ${activeTab === 'credentials' ? 'active' : ''}`}
                  onClick={() => { setActiveTab('credentials'); setError(null); }}
                  disabled={isLoading}
                >
                  {t('discordLoginTabCredentials')}
                </button>
                <button
                  type="button"
                  role="tab"
                  aria-selected={activeTab === 'token'}
                  className={`login-tab ${activeTab === 'token' ? 'active' : ''}`}
                  onClick={() => { setActiveTab('token'); setError(null); }}
                  disabled={isLoading}
                >
                  {t('discordLoginTabToken')}
                </button>
              </div>
            )}

            {error && (
              <div className="auth-error-banner" role="alert">
                {error}
              </div>
            )}

            {captchaChallenge ? (
              <div className="discord-captcha-view">
                <p className="discord-captcha-prompt">{t('discordCaptchaPrompt')}</p>
                <p className="discord-captcha-note">{t('discordCaptchaNote')}</p>

                <div className="discord-captcha-wrapper">
                  <div ref={captchaContainerRef} id="hcaptcha-container" />
                </div>

                {isLoading && (
                  <p className="discord-captcha-verifying">{t('discordVerifying')}</p>
                )}

                <div className="form-actions" style={{ width: '100%', justifyContent: 'space-between', marginTop: 12 }}>
                  <Button
                    variant="ghost"
                    type="button"
                    onClick={() => { setCaptchaChallenge(null); setError(null); }}
                    disabled={isLoading}
                  >
                    {t('back')}
                  </Button>
                  <Button
                    variant="ghost"
                    type="button"
                    onClick={() => { setCaptchaChallenge(null); setActiveTab('token'); setError(null); }}
                    disabled={isLoading}
                  >
                    {t('discordLoginTabToken')}
                  </Button>
                </div>
              </div>
            ) : mfaTicket ? (
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
            ) : activeTab === 'credentials' ? (
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
            ) : (
              <form onSubmit={handleTokenSubmit} className="discord-auth-form">
                <div className="form-group">
                  <label htmlFor="discord-token-input">{t('discordTokenInputLabel')}</label>
                  <input
                    id="discord-token-input"
                    type="password"
                    autoComplete="off"
                    autoCapitalize="none"
                    spellCheck={false}
                    className="text-input mono-text"
                    placeholder={t('discordTokenInputPlaceholder')}
                    value={tokenInput}
                    onChange={(e) => setTokenInput(e.target.value)}
                    autoFocus
                    required
                    disabled={isLoading}
                  />
                  <p className="form-hint">{t('discordTokenInputHint')}</p>
                </div>
                <div className="form-actions">
                  <Button variant="ghost" type="button" onClick={onClose} disabled={isLoading}>
                    {t('cancel')}
                  </Button>
                  <Button variant="primary" type="submit" disabled={isLoading || !tokenInput.trim()}>
                    {isLoading ? t('discordSavingToken') : t('discordSaveTokenBtn')}
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
