import React, { useState } from 'react';
import { useAuth } from './context/AuthContext';
import { I18nProvider, useI18n } from './context/I18nContext';
import { Shell } from './components/layout/Shell';
import type { NavTab } from './components/layout/Sidebar';
import { LoginView } from './views/LoginView';
import { AccountView } from './views/AccountView';
import { QuestsView } from './views/QuestsView';
import { SnowflakesView } from './views/SnowflakesView';

import { IosConnectionBanner } from './components/common/IosConnectionBanner';

function AppContent() {
  const { isAuthenticated, isLoading, user } = useAuth();
  const { t } = useI18n();
  const [activeTab, setActiveTab] = useState<NavTab>('account');

  return (
    <>
      <IosConnectionBanner />
      {isLoading ? (
        <div className="auth-screen">
          <span className="wordmark">{t('brandName')}</span>
          <p className="loading-text" role="status">
            {t('loadingAccount')}
          </p>
        </div>
      ) : !isAuthenticated ? (
        <LoginView />
      ) : (
        <Shell activeTab={activeTab} onNavigate={setActiveTab}>
          {activeTab === 'account' && <AccountView />}
          {activeTab === 'quests' && <QuestsView />}
          {activeTab === 'snowflakes' && <SnowflakesView />}
        </Shell>
      )}
    </>
  );
}

export function App() {
  const { user } = useAuth();

  return (
    <I18nProvider userLanguage={user?.language}>
      <AppContent />
    </I18nProvider>
  );
}
