import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { progress } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, motion, sec, type } from '../design/tokens';
import { intro } from '../shots/timeline';

export const introFrames = sec(intro.seconds);

export const Intro: React.FC = () => {
  const frame = useCurrentFrame();
  const fade = (delay: number) => ({ opacity: progress(frame, delay, motion.base) });

  return (
    <AbsoluteFill
      style={{
        background: colors.background,
        justifyContent: 'center',
        paddingLeft: layout.introLeft,
        fontFamily: fonts.sans,
        color: colors.textPrimary,
      }}
    >
      <div style={{ fontSize: type.display, fontWeight: 'bold', lineHeight: 1, ...fade(sec(0.2)) }}>{intro.title}</div>
      <div style={{ fontSize: type.title, marginTop: 34, ...fade(sec(0.6)) }}>{intro.author}</div>
      <div style={{ fontSize: type.body, color: colors.textSecondary, marginTop: 78, ...fade(sec(1.1)) }}>
        {intro.capture}
      </div>
    </AbsoluteFill>
  );
};
