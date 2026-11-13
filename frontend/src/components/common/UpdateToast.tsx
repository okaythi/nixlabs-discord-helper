import React, { useEffect, useState, useCallback } from 'react';
import { useI18n } from '../../context/I18nContext';
import {
  computeUpdateSubtitle,
  dismissUpdate,
  getUpdateStatus,
  isDesktopBackend,
  isUpdateModalVisible,
  startUpdate,
  subscribeUpdateEvents,
  type UpdateStatus,
} from '../../api/updateApi';
import { Button } from './Button';
import { IconCheckCircle, IconDownload } from '../icons';

export const UpdateToast: React.FC = () => {
  const { t } = useI18n();
  const [status, setStatus] = useState<UpdateStatus | null>(null);
  const [isStarting, setIsStarting] = useState(false);

  useEffect(() => {
    if (!isDesktopBackend()) return;
    let active = true;

    getUpdateStatus()
      .then((res) => {
        if (active && res) setStatus(res);
      })
      .catch(() => {
        // Ignore offline/startup update check errors
      });

    const unsubscribe = subscribeUpdateEvents((next) => {
      if (active) setStatus(next);
    });

    return () => {
      active = false;
      unsubscribe();
    };
  }, []);

  const isUpdating = status?.phase === 'downloading' || status?.phase === 'installing';

  useEffect(() => {
    if (!isUpdating) return;
    let active = true;
    const interval = window.setInterval(() => {
      getUpdateStatus()
        .then((res) => {
          if (active && res) setStatus(res);
        })
        .catch(() => {});
    }, 300);
    return () => {
      active = false;
      window.clearInterval(interval);
    };
  }, [isUpdating]);

  const handleUpdate = useCallback(async () => {
    setIsStarting(true);
    try {
      const next = await startUpdate();
      setStatus(next);
    } catch (err: any) {
      setStatus((prev) =>
        prev
          ? { ...prev, phase: 'error', error: err?.message || 'Update failed' }
          : null
      );
    } finally {
      setIsStarting(false);
    }
  }, []);

  const handleDismiss = useCallback(async () => {
    setStatus((prev) => (prev ? { ...prev, dismissed: true } : null));
    try {
      await dismissUpdate();
    } catch {
      // Already hidden locally for this launch
    }
  }, []);

  if (!isUpdateModalVisible(status) || !status) {
    return null;
  }

  const cleanVersion = (status.latest_version || '').replace(/^v/i, '');
  const isCompleted = status.phase === 'completed';
  const showProgress = isUpdating || isCompleted;
  const clampedPercent = isCompleted
    ? 100
    : Math.min(100, Math.max(0, Math.round(status.percent || 0)));
  const subtitle = computeUpdateSubtitle(status.phase, status.download_percent || 0, t);

  return (
    <aside
      className={`update-toast${isCompleted ? ' is-completed' : ''}`}
      role="dialog"
      aria-live="polite"
      aria-label={t('updateAvailableTitle')}
    >
      <div className="update-toast-header">
        <span className={`update-toast-icon${isCompleted ? ' is-completed' : ''}`} aria-hidden="true">
          {isCompleted ? <IconCheckCircle size={18} /> : <IconDownload size={18} />}
        </span>
        <div className="update-toast-titles">
          <p className="update-toast-title">{t('updateAvailableTitle')}</p>
          {cleanVersion && (
            <p className="update-toast-version">
              {t('updateVersionLabel', { version: cleanVersion })}
            </p>
          )}
        </div>
      </div>

      {status.phase === 'error' && status.error && (
        <p className="update-toast-error" role="alert">
          {status.error}
        </p>
      )}

      {showProgress ? (
        <div className="update-toast-progress">
          <div
            className="progress-track update-toast-track"
            role="progressbar"
            aria-valuemin={0}
            aria-valuemax={100}
            aria-valuenow={clampedPercent}
            aria-valuetext={subtitle}
          >
            <div
              className={`progress-fill update-toast-fill${isCompleted ? ' is-completed' : ''}`}
              style={{ width: `${clampedPercent}%` }}
            />
          </div>
          <p className="update-toast-subtitle">{subtitle}</p>
        </div>
      ) : null}

      <div className="update-toast-actions">
        {isCompleted ? (
          <Button
            variant="primary"
            className="update-toast-btn"
            onClick={handleDismiss}
          >
            {t('doneBtn')}
          </Button>
        ) : !isUpdating ? (
          <>
            <Button
              variant="ghost"
              className="update-toast-btn"
              onClick={handleDismiss}
              disabled={isStarting}
            >
              {t('remindMeLaterBtn')}
            </Button>
            <Button
              variant="primary"
              className="update-toast-btn"
              onClick={handleUpdate}
              isLoading={isStarting}
            >
              {t('updateBtn')}
            </Button>
          </>
        ) : null}
      </div>
    </aside>
  );
};
