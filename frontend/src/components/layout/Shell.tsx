import React, { ReactNode } from 'react';
import { Header } from './Header';
import { Sidebar, type NavTab } from './Sidebar';

interface ShellProps {
  activeTab: NavTab;
  onNavigate: (tab: NavTab) => void;
  children: ReactNode;
}

export const Shell: React.FC<ShellProps> = ({ activeTab, onNavigate, children }) => {
  return (
    <div className="app-shell">
      <Header />
      <div className="app-body">
        <Sidebar activeTab={activeTab} onSelectTab={onNavigate} />
        <main id="main" className="main-content" tabIndex={-1}>
          {children}
        </main>
      </div>
    </div>
  );
};
