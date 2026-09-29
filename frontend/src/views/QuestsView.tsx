import React, { useEffect, useState, useCallback } from 'react';
import { useI18n } from '../context/I18nContext';
import { useAuth } from '../context/AuthContext';
import { cacheKey, readRefreshCache, writeRefreshCache, REFRESH_INTERVAL_MS } from '../api/localRefreshCache';
import { discordTokenErrorMessage, formatQuestType } from '../i18n';
import {
  getQuests,
  completeQuest,
  completeAllQuests,
  cancelQuest,
  subscribeQuestProgress,
} from '../api/questsApi';
import type { Quest, QuestProgressEvent } from '../types/quests';
import { Button } from '../components/common/Button';
import { ProgressBar } from '../components/common/ProgressBar';
import {
  IconSparkles,
  IconPlay,
  IconRefresh,
  IconCheckCircle,
} from '../components/icons';

export const QuestsView: React.FC = () => {
  const { t } = useI18n();
  const { user } = useAuth();
  const storageKey = user ? cacheKey('quests', user.id || user.handle) : null;

  const [quests, setQuests] = useState<Quest[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const [activeProgress, setActiveProgress] = useState<QuestProgressEvent | null>(null);
  const [isStartingQuestId, setIsStartingQuestId] = useState<string | null>(null);
  const [isStartingAll, setIsStartingAll] = useState(false);
  const [isStopping, setIsStopping] = useState(false);

  const [checkedAt, setCheckedAt] = useState(0);

  const fetchQuestsList = useCallback(async (explicitRefresh = false) => {
    if (!storageKey) return;
    const cached = readRefreshCache<Quest[]>(storageKey);
    if (!explicitRefresh && Date.now() - cached.checkedAt < REFRESH_INTERVAL_MS) return;
    const now = Date.now();
    writeRefreshCache(storageKey, { ...cached, checkedAt: now });
    setCheckedAt(now);
    setIsLoading(cached.value === null);
    setError(null);
    try {
      const res = await getQuests(true);
      const next = res.quests || [];
      setQuests(next);
      writeRefreshCache(storageKey, { checkedAt: now, value: next });
    } catch (err: any) {
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to load quests');
    } finally {
      setIsLoading(false);
    }
  }, [storageKey, t]);

  useEffect(() => {
    if (!storageKey) return;
    const cached = readRefreshCache<Quest[]>(storageKey);
    setQuests(cached.value || []);
    setCheckedAt(cached.checkedAt);
    setIsLoading(cached.value === null && cached.checkedAt === 0);
  }, [storageKey]);

  useEffect(() => {
    if (!storageKey || activeProgress?.running) return;
    const remaining = Math.max(0, readRefreshCache<Quest[]>(storageKey).checkedAt + REFRESH_INTERVAL_MS - Date.now());
    if (remaining === 0) {
      void fetchQuestsList();
      return;
    }
    const timer = window.setTimeout(() => void fetchQuestsList(), remaining);
    return () => window.clearTimeout(timer);
  }, [storageKey, checkedAt, activeProgress?.running, fetchQuestsList]);

  useEffect(() => {
    const unsubscribe = subscribeQuestProgress((event) => {
      setActiveProgress(event);
      if (event.error) setError(event.error);
      if (event.quest_id && !event.estimated) {
        setQuests((current) => {
          const updated = current.map((quest) => quest.id === event.quest_id
            ? { ...quest, seconds_done: Math.max(quest.seconds_done, event.seconds_done),
                enrolled: true, completed: quest.completed || event.completed }
            : quest);
          if (storageKey && updated.some((quest, index) => quest !== current[index])) {
            const cached = readRefreshCache<Quest[]>(storageKey);
            writeRefreshCache(storageKey, { ...cached, value: updated });
          }
          return updated;
        });
      }
      if (!event.running) {
        setIsStartingQuestId(null);
        setIsStartingAll(false);
        setIsStopping(false);
      }
    });
    return unsubscribe;
  }, [storageKey]);

  const handleStartSingleQuest = async (questId: string) => {
    setIsStartingQuestId(questId);
    setError(null);
    try {
      await completeQuest(questId);
    } catch (err: any) {
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to start quest');
      setIsStartingQuestId(null);
    }
  };

  const handleStartAllQuests = async () => {
    setIsStartingAll(true);
    setError(null);
    try {
      await completeAllQuests();
    } catch (err: any) {
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to start all quests');
      setIsStartingAll(false);
    }
  };

  const handleStopQuest = async () => {
    setIsStopping(true);
    try {
      await cancelQuest();
    } catch (err: any) {
      console.error('Failed to cancel quest:', err);
    } finally {
      setIsStopping(false);
    }
  };

  const completableQuests = quests.filter((q) => q.completable && !q.completed);
  const isAnyRunning = activeProgress?.running ?? false;

  return (
    <div className="quests-view">
      <div className="view-header-row">
        <div>
          <h1 className="view-title">{t('questsTitle')}</h1>
          <p className="view-subtitle">{t('questsSubtitle')}</p>
        </div>

        <div className="view-header-actions">
          <Button
            variant="ghost"
            onClick={() => fetchQuestsList(true)}
            disabled={isLoading || isAnyRunning}
            title={t('refreshQuests')}
          >
            <IconRefresh size={16} />
            <span>{t('refreshQuests')}</span>
          </Button>

          {completableQuests.length > 0 && (
            <Button
              variant="primary"
              onClick={handleStartAllQuests}
              disabled={isAnyRunning || isStartingAll || isLoading}
            >
              <IconSparkles size={16} />
              <span>{isStartingAll ? t('running') : t('completeAll')}</span>
            </Button>
          )}
        </div>
      </div>

      {error && (
        <div className="auth-error-banner" role="alert">
          {error}
        </div>
      )}

      {/* ── Active Progress Widget ── */}
      {isAnyRunning && activeProgress && (
        <ProgressBar
          questName={activeProgress.quest_name}
          taskType={activeProgress.task_type}
          secondsDone={activeProgress.seconds_done}
          secondsNeeded={activeProgress.seconds_needed}
          percent={activeProgress.percent}
          estimated={activeProgress.estimated}
          subtext={activeProgress.status_text}
          onStop={handleStopQuest}
          isStopping={isStopping}
        />
      )}

      {/* ── Quests List or Empty State ── */}
      {isLoading ? (
        <div className="quests-loading-state">
          <div className="pulse-indicator" aria-hidden="true" />
          <p>{t('loadingDetails')}</p>
        </div>
      ) : quests.length === 0 ? (
        <div className="no-quests-card">
          <div className="no-quests-icon tone-purple">
            <IconSparkles size={32} />
          </div>
          <h2 className="no-quests-title">{t('noQuestsFound')}</h2>
          <p className="no-quests-desc">{t('noQuestsDescription')}</p>
          <Button variant="secondary" onClick={() => fetchQuestsList(true)}>
            {t('refreshQuests')}
          </Button>
        </div>
      ) : (
        <div className="quests-grid">
          {quests.map((quest) => {
            const isThisRunning = isAnyRunning && activeProgress?.quest_id === quest.id;
            const canComplete = quest.completable && !quest.completed;

            return (
              <div
                key={quest.id}
                className={`quest-card${quest.completed ? ' is-completed' : ''}${isThisRunning ? ' is-active' : ''}`}
              >
                {quest.hero_url && (
                  <div
                    className="quest-hero-banner"
                    style={{
                      backgroundImage: `url(${quest.hero_url})`,
                      backgroundColor: quest.colors?.primary || '#4752c4',
                    }}
                  />
                )}

                <div className="quest-card-body">
                  <div className="quest-card-top">
                    <span className="quest-task-type">{formatQuestType(quest.task_type, t)}</span>
                    {quest.orb_quantity && (
                      <span className="quest-reward-pill">
                        {t('rewardBadge', { count: quest.orb_quantity })}
                      </span>
                    )}
                  </div>

                  <h3 className="quest-title">{quest.name}</h3>
                  <p className="quest-game-title">{quest.game_title}</p>

                  <div className="quest-card-footer">
                    {quest.completed ? (
                      <div className="quest-status-completed">
                        <IconCheckCircle size={18} />
                        <span>{t('questCompleted')}</span>
                      </div>
                    ) : (
                      <Button
                        variant={canComplete ? 'primary' : 'secondary'}
                        onClick={() => handleStartSingleQuest(quest.id)}
                        disabled={!canComplete || isAnyRunning || isStartingQuestId === quest.id}
                        className="quest-action-btn"
                      >
                        <IconPlay size={14} />
                        <span>
                          {isThisRunning
                            ? t('running')
                            : isStartingQuestId === quest.id
                            ? t('running')
                            : t('completeQuest')}
                        </span>
                      </Button>
                    )}
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
};
