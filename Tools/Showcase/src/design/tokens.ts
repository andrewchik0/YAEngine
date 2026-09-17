export const VIDEO = {
  width: 1920,
  height: 1080,
  fps: 60,
} as const;

export const sec = (seconds: number) => Math.round(seconds * VIDEO.fps);

// The editor's Lavender Dark palette, so the video and the engine read as one thing
// (Core/Source/Editor/Utils/EditorTheme.cpp)
export const colors = {
  background: '#1F1E23',
  surface: '#2C2B31',
  surfaceRaised: '#3A383F',
  border: '#45434B',
  borderStrong: '#5C5A63',
  textPrimary: '#E0DEE6',
  textSecondary: '#A09DAA',
  textTertiary: '#716E7B',
  accent: '#A99BDB',
  accentSoft: '#453D66',
  success: '#5FB865',
  warning: '#E5A84B',
  // Labels sit on footage, not on a themed surface, so their plate is neutral
  labelPlate: 'rgba(0, 0, 0, 0.72)',
} as const;

export const type = {
  display: 104,
  title: 44,
  heading: 38,
  body: 34,
  label: 31,
  caption: 20,
} as const;

export const layout = {
  safeX: 96,
  safeY: 64,
  gap: 32,
  radius: 14,
  // Mode labels run off the left edge of the frame; only the text is inset
  labelTextInset: 64,
  labelBottom: 104,
  labelHeight: 84,
  // The lavender block that closes a label, and the line that leads the wipe
  accentBarWidth: 13,
  wipeLineWidth: 3,
  // Left margin of the title card, wider than the safe area so the block reads as set in
  introLeft: 160,
} as const;

// One motion language for the whole video: everything moves at a constant speed. Nothing
// eases, nothing overshoots - an eased sweep reads as a designed flourish, a linear one as
// a readout.
export const motion = {
  fast: sec(0.2),
  base: sec(0.4),
  slow: sec(0.7),
  segmentTransition: sec(0.3),
} as const;
