import React from 'react';
import ReactDOM from 'react-dom/client';
import { App } from './App';
import { AuthProvider } from './context/AuthContext';
import { ThemeProvider } from './context/ThemeContext';
import './styles/reset.css';
import './styles/variables.css';
import './styles/global.css';
import './styles/app.css';

// ── Strict Zoom Lock: Prevent any browser-level zoom in DOM ──
window.addEventListener(
  'wheel',
  (e) => {
    if (e.ctrlKey) {
      e.preventDefault();
    }
  },
  { passive: false }
);

window.addEventListener('keydown', (e) => {
  if (
    e.ctrlKey &&
    (e.key === '+' ||
      e.key === '-' ||
      e.key === '=' ||
      e.key === '_' ||
      e.key === '0' ||
      e.code === 'NumpadAdd' ||
      e.code === 'NumpadSubtract' ||
      e.code === 'Numpad0')
  ) {
    e.preventDefault();
  }
});

// Disable WebKit pinch-to-zoom gesture events
window.addEventListener('gesturestart', (e) => {
  e.preventDefault();
});
window.addEventListener('gesturechange', (e) => {
  e.preventDefault();
});

const rootElement = document.getElementById('root');
if (rootElement) {
  ReactDOM.createRoot(rootElement).render(
    <React.StrictMode>
      <ThemeProvider>
        <AuthProvider>
          <App />
        </AuthProvider>
      </ThemeProvider>
    </React.StrictMode>
  );
}
