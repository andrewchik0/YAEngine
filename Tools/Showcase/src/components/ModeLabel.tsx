import React from 'react';
import { useCurrentFrame } from 'remotion';
import { progress } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, type } from '../design/tokens';
import type { Mode } from '../shots/timeline';

// Renderer + AA label. It runs off the left edge of the frame and is closed by a lavender
// block; the caller places it with ModeLabelAnchor.
export const ModeLabel: React.FC<{ mode: Mode }> = ({ mode }) => (
  <div
    style={{
      gridArea: '1 / 1',
      display: 'flex',
      alignItems: 'center',
      height: layout.labelHeight,
      background: colors.labelPlate,
      paddingLeft: layout.labelTextInset,
      whiteSpace: 'nowrap',
    }}
  >
    <div style={{ fontFamily: fonts.sans, fontSize: type.heading, color: colors.textPrimary }}>{mode.title}</div>
    <div
      style={{ fontFamily: fonts.sans, fontSize: type.label, color: colors.textSecondary, marginLeft: 22, marginRight: 38 }}
    >
      {mode.detail}
    </div>
    {/* Pushed to the end of the plate, so it holds still while the line swaps the text */}
    <div style={{ width: layout.accentBarWidth, alignSelf: 'stretch', background: colors.accent, marginLeft: 'auto' }} />
  </div>
);

// Holds the labels against the bottom left corner. They are stacked in one grid cell, so every
// label is as wide as the widest of them and the plate and the lavender block stay put while
// the sweep line swaps the text under itself. The top-left corner is left to the PresentMon
// overlay burned into the footage.
export const ModeLabelAnchor: React.FC<{ children: React.ReactNode; delay?: number }> = ({ children, delay = 0 }) => {
  const frame = useCurrentFrame();
  return (
    <div
      style={{
        position: 'absolute',
        left: 0,
        bottom: layout.labelBottom,
        display: 'grid',
        justifyContent: 'start',
        opacity: progress(frame, delay, 12),
      }}
    >
      {children}
    </div>
  );
};
