import React from 'react';
import { useCurrentFrame } from 'remotion';
import { enterStyle } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, motion, type } from '../design/tokens';

type LowerThirdProps = {
  title: string;
  detail?: string;
  detailDelay?: number;
  italicDetail?: boolean;
};

export const LowerThird: React.FC<LowerThirdProps> = ({ title, detail, detailDelay = motion.base, italicDetail }) => {
  const frame = useCurrentFrame();
  return (
    <div style={{ position: 'absolute', left: layout.safeX, bottom: layout.safeY + 8 }}>
      <div
        style={{
          fontFamily: fonts.sans,
          fontWeight: 700,
          fontSize: type.title,
          color: colors.textPrimary,
          textShadow: '0 2px 24px rgba(0, 0, 0, 0.55)',
          ...enterStyle(frame, 0),
        }}
      >
        {title}
      </div>
      {detail ? (
        <div
          style={{
            fontFamily: fonts.sans,
            fontSize: type.body,
            fontStyle: italicDetail ? 'italic' : 'normal',
            color: colors.textSecondary,
            marginTop: 6,
            textShadow: '0 2px 16px rgba(0, 0, 0, 0.6)',
            ...enterStyle(frame, detailDelay),
          }}
        >
          {detail}
        </div>
      ) : null}
    </div>
  );
};
