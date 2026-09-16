import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { enterStyle, mix, progress } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, motion, sec, type } from '../design/tokens';
import { intro } from '../shots/timeline';

export const introFrames = sec(intro.seconds);

export const Intro: React.FC = () => {
  const frame = useCurrentFrame();
  const reveal = progress(frame, sec(0.2), motion.slow);
  const bar = progress(frame, sec(0.5), motion.slow);

  return (
    <AbsoluteFill
      style={{
        background: `radial-gradient(ellipse at 20% 50%, #1a1410 0%, ${colors.background} 60%)`,
        justifyContent: 'center',
        paddingLeft: layout.safeX * 2,
      }}
    >
      <div style={{ overflow: 'hidden', paddingBottom: 8 }}>
        <div
          style={{
            fontFamily: fonts.sans,
            fontWeight: 700,
            fontSize: type.display,
            letterSpacing: '-0.03em',
            color: colors.textPrimary,
            transform: `translateY(${mix(110, 0, reveal)}%)`,
          }}
        >
          {intro.title}
        </div>
      </div>
      <div style={{ width: mix(0, 140, bar), height: 4, background: colors.accent, margin: '18px 0 26px' }} />
      <div
        style={{
          fontFamily: fonts.sans,
          fontSize: type.heading,
          color: colors.textSecondary,
          ...enterStyle(frame, sec(0.6)),
        }}
      >
        {intro.author}
      </div>
      <div
        style={{
          fontFamily: fonts.mono,
          fontSize: type.label,
          color: colors.textTertiary,
          marginTop: 56,
          letterSpacing: 1,
          ...enterStyle(frame, sec(1.1)),
        }}
      >
        {intro.capture}
      </div>
    </AbsoluteFill>
  );
};
