import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { enterStyle, exitStyle, progress } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, motion, type, VIDEO } from '../design/tokens';

type CalloutProps = {
  x: number;
  y: number;
  title: string;
  detail: string;
  durationInFrames: number;
};

const RING_RADIUS = 22;
const LABEL_OFFSET = { x: -300, y: -170 };

// Curved arrow from a label to a point in the frame, with a ring around the point
export const Callout: React.FC<CalloutProps> = ({ x, y, title, detail, durationInFrames }) => {
  const frame = useCurrentFrame();

  const start = { x: x + LABEL_OFFSET.x + 40, y: y + LABEL_OFFSET.y + 20 };
  const control = { x: x - 40, y: y + LABEL_OFFSET.y - 10 };
  const toTarget = { x: x - control.x, y: y - control.y };
  const toTargetLength = Math.hypot(toTarget.x, toTarget.y);
  const end = {
    x: x - (toTarget.x / toTargetLength) * (RING_RADIUS + 10),
    y: y - (toTarget.y / toTargetLength) * (RING_RADIUS + 10),
  };
  const headAngle = (Math.atan2(end.y - control.y, end.x - control.x) * 180) / Math.PI;

  const ring = progress(frame, 0, motion.base);
  const draw = progress(frame, motion.fast, motion.slow);
  const headVisible = draw > 0.95 ? 1 : 0;
  const exitStart = durationInFrames - motion.base;

  return (
    <AbsoluteFill style={exitStyle(frame, exitStart, motion.base)}>
      <svg width={VIDEO.width} height={VIDEO.height} style={{ position: 'absolute', inset: 0 }}>
        <circle
          cx={x}
          cy={y}
          r={RING_RADIUS * ring}
          fill="none"
          stroke={colors.accent}
          strokeWidth={3}
          opacity={Math.min(1, ring)}
        />
        <path
          d={`M ${start.x} ${start.y} Q ${control.x} ${control.y} ${end.x} ${end.y}`}
          fill="none"
          stroke={colors.textPrimary}
          strokeWidth={3}
          strokeLinecap="round"
          pathLength={1}
          strokeDasharray={1}
          strokeDashoffset={1 - draw}
        />
        <polygon
          points="0,-8 14,0 0,8"
          fill={colors.textPrimary}
          opacity={headVisible}
          transform={`translate(${end.x} ${end.y}) rotate(${headAngle})`}
        />
      </svg>
      <div
        style={{
          position: 'absolute',
          left: x + LABEL_OFFSET.x - 220,
          top: y + LABEL_OFFSET.y - 70,
          width: 260,
          textAlign: 'right',
          ...enterStyle(frame, motion.base),
        }}
      >
        <div style={{ fontFamily: fonts.sans, fontWeight: 700, fontSize: type.heading - 4, color: colors.textPrimary }}>
          {title}
        </div>
        <div style={{ fontFamily: fonts.sans, fontSize: type.label + 2, color: colors.textSecondary, marginTop: 4 }}>
          {detail}
        </div>
      </div>
    </AbsoluteFill>
  );
};
