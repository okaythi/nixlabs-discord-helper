import React, { useState } from 'react';

interface AvatarProps {
  src?: string;
  name?: string;
  size?: 'sm' | 'md' | 'lg' | 'xl';
  className?: string;
}

export const Avatar: React.FC<AvatarProps> = ({
  src,
  name = 'User',
  size = 'md',
  className = '',
}) => {
  const [hasError, setHasError] = useState(false);
  const initial = (name || 'U').charAt(0).toUpperCase();

  return (
    <span className={`avatar avatar--${size} ${className}`} role="img" aria-label={name}>
      <span className="avatar-image">
        {src && !hasError ? (
          <img src={src} alt={name} onError={() => setHasError(true)} />
        ) : (
          initial
        )}
      </span>
    </span>
  );
};
