import React, { useState } from 'react';
import { useAuth } from '../../context/AuthContext';
import { useI18n } from '../../context/I18nContext';

export const IosConnectionBanner: React.FC = () => {
  const { isDisconnected, checkConnection } = useAuth();
  const { t } = useI18n();
  const [isRetrying, setIsRetrying] = useState(false);

  if (!isDisconnected) {
    return null;
  }

  const handleRetry = async () => {
    if (isRetrying) return;
    setIsRetrying(true);
    try {
      await checkConnection();
    } finally {
      setIsRetrying(false);
    }
  };

  return (
    <div
      className="ios-connection-banner"
      role="alert"
      aria-live="assertive"
      onClick={handleRetry}
      title={t('retry')}
    >
      <div className="ios-connection-banner-inner">
        <svg
          className={`ios-connection-icon ${isRetrying ? 'spin' : ''}`}
          width="16"
          height="16"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          strokeWidth="2.2"
          strokeLinecap="round"
          strokeLinejoin="round"
          aria-hidden="true"
        >
          <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z" />
          <line x1="12" y1="9" x2="12" y2="13" />
          <line x1="12" y1="17" x2="12.01" y2="17" />
        </svg>
        <span className="ios-connection-text">
          {t('connectionFailedBanner')}
        </span>
      </div>
    </div>
  );
};
