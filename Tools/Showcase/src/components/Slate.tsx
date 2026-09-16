import React from 'react';
import { AbsoluteFill } from 'remotion';
import { fonts } from '../design/fonts';
import { colors, type } from '../design/tokens';

type SlateProps = {
  title: string;
  detail?: string;
  // 0..1, lets a stack of placeholders read as "more light" without real images
  brightness?: number;
  // Which half of a wipe the label belongs to
  align?: 'center' | 'left' | 'right';
  style?: React.CSSProperties;
};

// Sizes follow the slate's own width, so the same slate reads full screen and in a thumbnail
const fluid = (min: number, max: number) => `clamp(${min}px, ${(max / 19.2).toFixed(2)}cqw, ${max}px)`;

// Stand-in for footage or stills that have not been captured yet
export const Slate: React.FC<SlateProps> = ({ title, detail, brightness = 0.15, align = 'center', style }) => {
  const glow = Math.round(18 + brightness * 60);
  return (
    <AbsoluteFill
      style={{
        background: `radial-gradient(ellipse at 50% 60%, hsl(24 40% ${glow}%) 0%, ${colors.surface} 75%)`,
        flexDirection: 'row',
        justifyContent: align === 'left' ? 'flex-start' : align === 'right' ? 'flex-end' : 'center',
        alignItems: 'center',
        overflow: 'hidden',
        containerType: 'size',
        ...style,
      }}
    >
      <AbsoluteFill
        style={{
          backgroundImage:
            'repeating-linear-gradient(135deg, rgba(255,255,255,0.035) 0 2px, transparent 2px 28px)',
        }}
      />
      <div
        style={{
          textAlign: 'center',
          padding: '0 4%',
          width: align === 'center' ? '100%' : '50%',
          wordBreak: 'break-all',
        }}
      >
        <div style={{ fontFamily: fonts.mono, fontSize: fluid(9, type.label), color: colors.warning, letterSpacing: 2 }}>
          PLACEHOLDER
        </div>
        <div
          style={{
            fontFamily: fonts.sans,
            fontWeight: 500,
            fontSize: fluid(14, type.heading),
            color: colors.textPrimary,
            marginTop: '0.3em',
            wordBreak: 'normal',
          }}
        >
          {title}
        </div>
        {detail ? (
          <div
            style={{ fontFamily: fonts.mono, fontSize: fluid(9, type.caption), color: colors.textSecondary, marginTop: '0.4em' }}
          >
            {detail}
          </div>
        ) : null}
      </div>
    </AbsoluteFill>
  );
};
