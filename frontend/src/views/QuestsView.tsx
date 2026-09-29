import React, { useEffect, useState, useCallback } from 'react';
import { useI18n } from '../context/I18nContext';
import { getDiscordUser } from '../api/discordApi';
import { cacheKey, clearRefreshCache, readRefreshCache, writeRefreshCache, REFRESH_INTERVAL_MS } from '../api/localRefreshCache';
import { discordTokenErrorMessage, formatQuestType } from '../i18n';
import {
  getQuests,
  getQuestProgress,
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
  const [discordId, setDiscordId] = useState<string | null>(null);
  const storageKey = discordId ? cacheKey('quests', discordId) : null;

  const [quests, setQuests] = useState<Quest[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [isRefreshing, setIsRefreshing] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [activeProgress, setActiveProgress] = useState<QuestProgressEvent | null>(null);
  const [isTrackingProgress, setIsTrackingProgress] = useState(false);
  const [isStartingQuestId, setIsStartingQuestId] = useState<string | null>(null);
  const [isStartingAll, setIsStartingAll] = useState(false);
  const [isStopping, setIsStopping] = useState(false);

  const [checkedAt, setCheckedAt] = useState(0);

  useEffect(() => {
    let active = true;
    getDiscordUser().then((profile) => {
      if (!profile.id) throw new Error('Discord account identity is unavailable');
      if (active) setDiscordId(profile.id);
    }).catch((err) => {
      if (!active) return;
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to verify Discord account');
      setIsLoading(false);
    });
    return () => { active = false; };
  }, [t]);

  const fetchQuestsList = useCallback(async (explicitRefresh = false) => {
    if (!storageKey) return;
    const cached = readRefreshCache<Quest[]>(storageKey);
    if (!explicitRefresh && Date.now() - cached.checkedAt < REFRESH_INTERVAL_MS) return;
    const now = Date.now();
    setIsLoading(cached.value === null);
    setIsRefreshing(explicitRefresh && cached.value !== null);
    setError(null);
    try {
      const res = await getQuests(true);
      const next = res.quests || [];
      setQuests(next);
      writeRefreshCache(storageKey, { checkedAt: now, value: next });
      setCheckedAt(now);
    } catch (err: any) {
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to load quests');
    } finally {
      setIsLoading(false);
      setIsRefreshing(false);
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

  const handleRefresh = async () => {
    setIsRefreshing(true);
    setError(null);
    try {
      const profile = await getDiscordUser(true);
      if (!profile.id) throw new Error('Discord account identity is unavailable');
      if (profile.id !== discordId) {
        const nextKey = cacheKey('quests', profile.id);
        clearRefreshCache(nextKey);
        setQuests([]);
        setIsLoading(true);
        setDiscordId(profile.id);
      } else {
        await fetchQuestsList(true);
      }
    } catch (err: any) {
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to refresh quests');
    } finally {
      setIsRefreshing(false);
    }
  };

  const applyProgressEvent = useCallback((event: QuestProgressEvent) => {
    if (!event.running && !event.status_text && !event.error) return;
    setActiveProgress(event);
    setIsTrackingProgress(event.running);
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
  }, [storageKey]);

  useEffect(() => subscribeQuestProgress(applyProgressEvent), [applyProgressEvent]);

  useEffect(() => {
    if (!discordId) return;
    let active = true;
    getQuestProgress().then((event) => {
      if (active) applyProgressEvent(event);
    }).catch(() => {});
    return () => { active = false; };
  }, [discordId, applyProgressEvent]);

  useEffect(() => {
    if (!isTrackingProgress) return;
    let inFlight = false;
    const timer = window.setInterval(async () => {
      if (inFlight) return;
      inFlight = true;
      try {
        applyProgressEvent(await getQuestProgress());
      } catch {
        // Keep the last known state; the next poll can recover the stream.
      } finally {
        inFlight = false;
      }
    }, 1000);
    return () => window.clearInterval(timer);
  }, [isTrackingProgress, applyProgressEvent]);

  const handleStartSingleQuest = async (questId: string) => {
    const quest = quests.find((item) => item.id === questId);
    setIsStartingQuestId(questId);
    setActiveProgress({
      quest_id: questId, quest_name: quest?.name || '', task_type: quest?.task_type || '',
      seconds_done: 0, seconds_needed: 0, percent: 0,
      status_text: t('loadingDetails'), running: true, completed: false,
    });
    setError(null);
    try {
      await completeQuest(questId);
      setIsTrackingProgress(true);
    } catch (err: any) {
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to start quest');
      setIsStartingQuestId(null);
      setActiveProgress(null);
    }
  };

  const handleStartAllQuests = async () => {
    setIsStartingAll(true);
    setActiveProgress({
      quest_id: '', quest_name: '', task_type: '', seconds_done: 0, seconds_needed: 0,
      percent: 0, status_text: t('loadingDetails'), running: true, completed: false,
    });
    setError(null);
    try {
      await completeAllQuests();
      setIsTrackingProgress(true);
    } catch (err: any) {
      setError(discordTokenErrorMessage(err, t) || err?.message || 'Failed to start all quests');
      setIsStartingAll(false);
      setActiveProgress(null);
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
            onClick={handleRefresh}
            disabled={isLoading || isRefreshing || isAnyRunning}
            title={t('refreshQuests')}
          >
            <span className={isRefreshing ? 'refresh-icon is-spinning' : 'refresh-icon'}>
              <IconRefresh size={16} />
            </span>
            <span>{t('refreshQuests')}</span>
          </Button>

          {completableQuests.length > 0 && (
            <Button
              variant="primary"
              onClick={handleStartAllQuests}
              disabled={isAnyRunning || isStartingAll || isLoading || isRefreshing || !!error}
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

      {isRefreshing && <p className="refresh-status" role="status">{t('loadingDetails')}</p>}

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
          <Button variant="secondary" onClick={handleRefresh} disabled={isRefreshing}>
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
                        disabled={!canComplete || isAnyRunning || isRefreshing || !!error || isStartingQuestId === quest.id}
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
