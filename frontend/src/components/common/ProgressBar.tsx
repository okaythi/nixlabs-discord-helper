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
  subtext,
  onStop,
  isStopping = false,
}) => {
  const { t } = useI18n();
  const clampedPercent = Math.min(100, Math.max(0, percent));

  return (
    <div className="quest-progress-card" role="region" aria-label={t('liveProgressTitle')}>
      <div className="quest-progress-header">
        <div className="quest-progress-meta">
          <span className="quest-progress-badge">{formatQuestType(taskType, t)}</span>
          <h3 className="quest-progress-title">{questName}</h3>
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

      <div className="progress-track" role="progressbar" aria-valuenow={clampedPercent} aria-valuemin={0} aria-valuemax={100}>
        <div
          className="progress-fill"
          style={{ width: `${clampedPercent}%` }}
        />
      </div>

      <div className="quest-progress-footer">
        <p className="quest-progress-subtext">{subtext}</p>
        <span className="quest-progress-counter">
          {Math.floor(secondsDone)}s / {secondsNeeded}s ({Math.round(clampedPercent)}%)
        </span>
      </div>
    </div>
  );
};
