import React from 'react';
import { AbsoluteFill, interpolate, useCurrentFrame } from 'remotion';
import { motion } from '../design/tokens';
import type { MediaPath, Mode } from '../shots/timeline';
import { Media } from './Media';
import { ModeLabel } from './ModeLabel';

type ABWipeProps = {
  a: MediaPath;
  b: MediaPath;
  aMode: Mode;
  bMode: Mode;
  durationInFrames: number;
  label?: string;
};

// A on the left, B revealed from the right; the divider settles near the middle and drifts slowly
export const ABWipe: React.FC<ABWipeProps> = ({ a, b, aMode, bMode, durationInFrames, label }) => {
  const frame = useCurrentFrame();
  const settle = Math.round(durationInFrames * 0.14);
  const split =
    frame < settle
      ? interpolate(frame, [0, settle], [1, 0.52], { extrapolateLeft: 'clamp', easing: motion.inOut })
      : interpolate(frame, [settle, durationInFrames], [0.52, 0.44], { extrapolateRight: 'clamp' });
  const percent = split * 100;

  const title = (mode: Mode) => (label ? `${label} - ${mode.title}` : mode.title);

  return (
    <AbsoluteFill>
      <Media src={a} label={title(aMode)} placeholderBrightness={0.1} placeholderAlign="left" />
      <AbsoluteFill style={{ clipPath: `inset(0 0 0 ${percent}%)` }}>
        <Media src={b} label={title(bMode)} placeholderBrightness={0.45} placeholderAlign="right" />
      </AbsoluteFill>
      <div
        style={{
          position: 'absolute',
          top: 0,
          bottom: 0,
          left: `${percent}%`,
          width: 2,
          marginLeft: -1,
          background: 'rgba(255, 255, 255, 0.9)',
          boxShadow: '0 0 18px rgba(0, 0, 0, 0.6)',
        }}
      />
      <ModeLabel mode={aMode} align="left" />
      <ModeLabel mode={bMode} align="right" delay={settle} />
    </AbsoluteFill>
  );
};
