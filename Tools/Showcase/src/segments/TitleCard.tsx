import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { progress } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, motion, sec, type } from '../design/tokens';
import type { TitleCardSpec } from '../shots/timeline';

// A statement between two parts of the video, set like the opening card so the two read as
// the same voice: flat background, one line, flush left, nothing else.
export const TitleCard: React.FC<{ card: TitleCardSpec }> = ({ card }) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill
      style={{
        background: colors.background,
        justifyContent: 'center',
        paddingLeft: layout.introLeft,
        paddingRight: layout.introLeft,
        fontFamily: fonts.sans,
        color: colors.textPrimary,
      }}
    >
      <div
        style={{
          fontSize: type.display - 20,
          fontWeight: 'bold',
          lineHeight: 1.1,
          opacity: progress(frame, sec(0.2), motion.base),
        }}
      >
        {card.title}
      </div>
      {card.detail ? (
        <div
          style={{
            fontSize: type.body,
            color: colors.textSecondary,
            marginTop: 40,
            opacity: progress(frame, sec(0.6), motion.base),
          }}
        >
          {card.detail}
        </div>
      ) : null}
    </AbsoluteFill>
  );
};

export const titleCardFrames = (card: TitleCardSpec) => sec(card.seconds);
