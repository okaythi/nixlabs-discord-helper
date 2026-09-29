import React from 'react';
import { IconUser, IconSparkles } from '../icons';
import { useI18n } from '../../context/I18nContext';

export type NavTab = 'account' | 'quests';

interface SidebarProps {
  activeTab: NavTab;
  onSelectTab: (tab: NavTab) => void;
}

export const Sidebar: React.FC<SidebarProps> = ({ activeTab, onSelectTab }) => {
  const { t } = useI18n();

  const navItems = [
    { id: 'account' as NavTab, label: t('tabAccount'), icon: IconUser, tone: 'tone-blue' },
    { id: 'quests' as NavTab, label: t('tabQuests'), icon: IconSparkles, tone: 'tone-purple' },
  ];

  return (
    <aside className="sidebar" aria-label="Navigation">
      <nav className="side-nav">
        {navItems.map(({ id, label, icon: Icon, tone }) => (
          <button
            key={id}
            className={`nav-item${activeTab === id ? ' is-active' : ''}`}
            onClick={() => onSelectTab(id)}
            aria-current={activeTab === id ? 'page' : undefined}
          >
            <span className={`nav-icon ${tone}`} aria-hidden="true">
              <Icon size={20} />
            </span>
            <span>{label}</span>
          </button>
        ))}
      </nav>

      <div className="sidebar-footer">
        <a
          href="https://nixlabs.tech"
          target="_blank"
          rel="noopener noreferrer"
          className="ecosystem-link"
        >
          {t('ecosystemLink')}
        </a>
      </div>
    </aside>
  );
};
