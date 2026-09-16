import { Easing } from 'remotion';

export const VIDEO = {
  width: 1920,
  height: 1080,
  fps: 60,
} as const;

export const sec = (seconds: number) => Math.round(seconds * VIDEO.fps);

export const colors = {
  background: '#07080a',
  surface: '#101217',
  surfaceRaised: '#171a21',
  border: 'rgba(255, 255, 255, 0.08)',
  borderStrong: 'rgba(255, 255, 255, 0.18)',
  textPrimary: '#f2f3f5',
  textSecondary: '#a3a8b3',
  textTertiary: '#6b7180',
  accent: '#ff6b35',
  accentSoft: 'rgba(255, 107, 53, 0.18)',
  success: '#5fd38d',
  warning: '#ffcc66',
} as const;

export const type = {
  display: 120,
  title: 56,
  heading: 36,
  body: 26,
  label: 20,
  caption: 16,
} as const;

export const layout = {
  safeX: 96,
  safeY: 64,
  gap: 32,
  radius: 14,
} as const;

// One motion language for the whole video: every enter uses `out`, every move uses `inOut`
export const motion = {
  out: Easing.bezier(0.16, 1, 0.3, 1),
  inOut: Easing.bezier(0.65, 0, 0.35, 1),
  fast: sec(0.2),
  base: sec(0.4),
  slow: sec(0.7),
  segmentTransition: sec(0.3),
} as const;
