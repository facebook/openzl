// Copyright (c) Meta Platforms, Inc. and affiliates.

import './Banner.css';

interface BannerProps {
  children: React.ReactNode;
  icon?: React.ReactNode;
}

export function Banner({children, icon = '🚧'}: BannerProps) {
  return (
    <div className="banner" role="status" aria-live="polite">
      <span aria-hidden="true">{icon}</span>
      <span>{children}</span>
    </div>
  );
}
