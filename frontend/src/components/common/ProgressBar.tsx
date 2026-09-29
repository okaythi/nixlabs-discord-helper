import React from 'react';
import { Button } from './Button';
import { IconSquare } from '../icons';
import { useI18n } from '../../context/I18nContext';
import { formatQuestType } from '../../i18n';

interface ProgressBarProps {
  questName: string;
  taskType: string;
  secondsDone: number;
  secondsNeeded: number;
  percent: number;
  estimated?: boolean;
  subtext: string;
  onStop?: () => void;
  isStopping?: boolean;
}

export const ProgressBar: React.FC<ProgressBarProps> = ({
  questName,
  taskType,
  secondsDone,
  secondsNeeded,
  percent,
  estimated = false,
  subtext,
  onStop,
  isStopping = false,
}) => {
  const { t } = useI18n();
  const hasMeasuredProgress = secondsNeeded > 0 && Number.isFinite(percent);
  const clampedPercent = hasMeasuredProgress ? Math.min(100, Math.max(0, percent)) : 0;

  return (
    <div className="quest-progress-card" role="region" aria-label={t('liveProgressTitle')}>
      <div className="quest-progress-header">
        <div className="quest-progress-meta">
          {taskType && <span className="quest-progress-badge">{formatQuestType(taskType, t)}</span>}
          <h3 className="quest-progress-title">{questName || t('loadingDetails')}</h3>
        </div>
        {onStop && (
          <Button
            variant="danger"
            onClick={onStop}
            disabled={isStopping}
            className="quest-stop-btn"
          >
            <IconSquare size={13} />
            <span>{isStopping ? t('subtextStopping') : t('stopQuest')}</span>
          </Button>
        )}
      </div>

      <div className="progress-track" role="progressbar" aria-valuenow={hasMeasuredProgress ? clampedPercent : undefined} aria-valuemin={0} aria-valuemax={100} aria-valuetext={hasMeasuredProgress ? undefined : subtext}>
        <div
          className={`progress-fill${hasMeasuredProgress ? '' : ' is-indeterminate'}`}
          style={{ width: `${clampedPercent}%` }}
        />
      </div>

      <div className="quest-progress-footer">
        <p className="quest-progress-subtext">{subtext}</p>
        {hasMeasuredProgress && <span className="quest-progress-counter">
          {estimated ? '~' : ''}{Math.floor(secondsDone)}s / {secondsNeeded}s ({estimated ? '~' : ''}{Math.round(clampedPercent)}%)
        </span>}
      </div>
    </div>
  );
};
