import React, { ButtonHTMLAttributes } from 'react';

export type ButtonVariant = 'primary' | 'secondary' | 'danger' | 'ghost';

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
  isLoading?: boolean;
}

export const Button: React.FC<ButtonProps> = ({
  variant = 'secondary',
  isLoading = false,
  className = '',
  children,
  disabled,
  type = 'button',
  ...props
}) => {
  return (
    <button
      type={type}
      className={`button button--${variant} ${className}`}
      disabled={disabled || isLoading}
      {...props}
    >
      {isLoading ? <span className="btn-spinner" aria-hidden="true" /> : null}
      {children}
    </button>
  );
};
