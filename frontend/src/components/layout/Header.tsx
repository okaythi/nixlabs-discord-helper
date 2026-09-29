import React from 'react';
import { useAuth } from '../../context/AuthContext';
import { useTheme } from '../../context/ThemeContext';
import { useI18n } from '../../context/I18nContext';
import { IconSun, IconMoon, IconLogOut } from '../icons';
import { Avatar } from '../common/Avatar';

export const Header: React.FC = () => {
  const { user, logout } = useAuth();
  const { theme, toggleTheme } = useTheme();
  const { t } = useI18n();

  return (
    <header className="app-header">
      <div className="wordmark">
        <span>{t('brandName')}</span>
        <span className="wordmark-divider" />
        <span className="wordmark-product">{t('productName')}</span>
      </div>

      <div className="header-actions">
        <button
          className="icon-button"
          onClick={toggleTheme}
          aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`}
          title="Theme"
        >
          {theme === 'dark' ? <IconSun /> : <IconMoon />}
        </button>

        {user && (
          <div className="header-user-badge" title={`${user.name} (@${user.handle})`}>
            <Avatar src={user.avatar || user.avatarUrl} name={user.name} size="sm" />
            <span className="header-username">@{user.handle}</span>
            <button
              className="icon-button header-logout-btn"
              onClick={logout}
              aria-label={t('signOut')}
              title={t('signOut')}
            >
              <IconLogOut size={16} />
            </button>
          </div>
        )}
      </div>
    </header>
  );
};
