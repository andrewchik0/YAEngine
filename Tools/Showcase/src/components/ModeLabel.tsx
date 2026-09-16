import React from 'react';
import { useCurrentFrame } from 'remotion';
import { enterStyle } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, type } from '../design/tokens';
import type { Mode } from '../shots/timeline';

type ModeLabelProps = {
  mode: Mode;
  align?: 'left' | 'right';
  delay?: number;
};

// Renderer + AA mode badge, sits in the header next to the PresentMon overlay
export const ModeLabel: React.FC<ModeLabelProps> = ({ mode, align = 'left', delay = 0 }) => {
  const frame = useCurrentFrame();
  return (
    <div
      style={{
        position: 'absolute',
        top: layout.safeY,
        [align]: layout.safeX,
        display: 'flex',
        alignItems: 'center',
        gap: 14,
        padding: '12px 18px',
        borderRadius: 10,
        background: 'rgba(7, 8, 10, 0.62)',
        border: `1px solid ${colors.border}`,
        backdropFilter: 'blur(12px)',
        ...enterStyle(frame, delay),
      }}
    >
      <div style={{ width: 10, height: 10, borderRadius: 2, background: colors.accent }} />
      <div style={{ fontFamily: fonts.sans, fontWeight: 500, fontSize: type.label + 2, color: colors.textPrimary }}>
        {mode.title}
      </div>
      <div style={{ fontFamily: fonts.mono, fontSize: type.caption, color: colors.textSecondary }}>{mode.detail}</div>
    </div>
  );
};
