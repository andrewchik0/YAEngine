import React from 'react';
import { AbsoluteFill } from 'remotion';
import { ClipSequence } from '../components/Media';
import { ModeLabel, ModeLabelAnchor } from '../components/ModeLabel';
import { colors, sec } from '../design/tokens';
import { flythrough } from '../shots/timeline';

export const flythroughFrames = flythrough.clips.reduce((sum, clip) => sum + sec(clip.seconds), 0);

// The PresentMon overlay is part of the OBS recording, so only the mode badge is added here
export const Flythrough: React.FC = () => (
  <AbsoluteFill style={{ background: colors.background }}>
    <ClipSequence clips={flythrough.clips} />
    <ModeLabelAnchor delay={sec(0.5)}>
      <ModeLabel mode={flythrough.mode} />
    </ModeLabelAnchor>
  </AbsoluteFill>
);
