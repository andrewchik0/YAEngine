import React, { useMemo } from 'react';
import { AbsoluteFill, Easing, interpolate, spring, useCurrentFrame, useVideoConfig } from 'remotion';
import { chevronPoints, CRUMB, measureText, Plate } from '../components/Breadcrumbs';
import { Media } from '../components/Media';
import { mix } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, sec, VIDEO } from '../design/tokens';
import { breakdownStages } from '../shots/timeline';

const { stages, changeSeconds } = breakdownStages;
const N = stages.length;
const CHANGE = sec(changeSeconds);
const BAR_TOP = 56;
const DIM = 0.4;
const DETAIL_SIZE = CRUMB.fontSize - 6;
const DETAIL_GAP = 16;

// Stage i + 1 starts replacing stage i here
const changeAt = (i: number) =>
  stages.slice(0, i + 1).reduce((sum, stage) => sum + sec(stage.holdSeconds), 0) + CHANGE * i;

export const breakdownStagesFrames = changeAt(N - 1);

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

// Unlike the rest of the video the change eases and springs: it is a short event between holds,
// not a readout that lasts as long as the hold
const wipe = (frame: number, i: number) =>
  interpolate(frame, [changeAt(i), changeAt(i) + CHANGE], [0, 1], {
    easing: Easing.inOut(Easing.cubic),
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
  });

// The stills, each wiped in over the previous one behind a lavender line
const Stills: React.FC = () => {
  const frame = useCurrentFrame();
  const reveal = (i: number) => (i === 0 ? 1 : wipe(frame, i - 1));
  const running = stages.findIndex((_, i) => i > 0 && reveal(i) > 0 && reveal(i) < 1);

  return (
    <AbsoluteFill>
      {stages.map((stage, i) => (
        <AbsoluteFill key={stage.src} style={i === 0 ? undefined : { clipPath: `inset(0 ${(1 - reveal(i)) * 100}% 0 0)` }}>
          <Media src={stage.src} label={stage.title} />
        </AbsoluteFill>
      ))}
      {running === -1 ? null : (
        <div
          style={{
            position: 'absolute',
            top: 0,
            bottom: 0,
            left: `${reveal(running) * 100}%`,
            width: layout.wipeLineWidth,
            marginLeft: -layout.wipeLineWidth / 2,
            background: colors.accent,
          }}
        />
      )}
    </AbsoluteFill>
  );
};

// Inactive stages fold down to their number; the active one opens up with its name and what it
// contributes, the neighbours making room on a spring
const StageBar: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const sizes = useMemo(
    () =>
      stages.map((stage, i) => ({
        number: Math.max(18, measureText(String(i + 1), CRUMB.fontSize)),
        full: measureText(stage.title, CRUMB.fontSize) + DETAIL_GAP + measureText(stage.detail, DETAIL_SIZE),
      })),
    [],
  );

  let t = 0;
  for (let i = 0; i < N - 1; i++) {
    if (frame >= changeAt(i)) t += spring({ frame: frame - changeAt(i), fps, config: { damping: 18, stiffness: 150, mass: 0.7 } });
  }

  let x = 0;
  const items = stages.map((stage, i) => {
    const notch = i === 0 ? 0 : CRUMB.point;
    const open = clamp01(1 - Math.abs(t - i));
    const width = notch + CRUMB.padX * 2 + mix(sizes[i].number, sizes[i].full, open) + CRUMB.point;
    const item = { stage, i, x, width, notch, open, points: chevronPoints(x, 0, width, CRUMB.height, notch, CRUMB.point) };
    x += width - CRUMB.point + CRUMB.gap;
    return item;
  });
  const left = (VIDEO.width - (x - CRUMB.gap + CRUMB.point)) / 2;
  const y = CRUMB.height / 2 + 1;

  return (
    <svg width={VIDEO.width} height={VIDEO.height} style={{ position: 'absolute', inset: 0 }}>
      <defs>
        {items.map(({ i, points }) => (
          <clipPath key={i} id={`stage-${i}`}>
            <polygon points={points} />
          </clipPath>
        ))}
      </defs>
      <g transform={`translate(${left} ${BAR_TOP})`}>
        {items.map(({ stage, i, x: ix, width, notch, open, points }) => (
          <g key={stage.title}>
            <Plate points={points} lit={open} outline={DIM} />
            <g clipPath={`url(#stage-${i})`}>
              <text
                x={ix + (notch + width - CRUMB.point) / 2}
                y={y}
                textAnchor="middle"
                dominantBaseline="central"
                fontFamily={fonts.sans}
                fontSize={CRUMB.fontSize}
                fill={colors.textPrimary}
                opacity={DIM * (1 - open) * (1 - open)}
              >
                {i + 1}
              </text>
              {/* The name only comes in once the crumb is open far enough to hold it */}
              <text
                x={ix + notch + CRUMB.padX}
                y={y}
                dominantBaseline="central"
                fontFamily={fonts.sans}
                opacity={clamp01((open - 0.45) / 0.4)}
              >
                <tspan fontSize={CRUMB.fontSize} fill={colors.background}>
                  {stage.title}
                </tspan>
                <tspan dx={DETAIL_GAP} fontSize={DETAIL_SIZE} fill={colors.background} fillOpacity={0.7}>
                  {stage.detail}
                </tspan>
              </text>
            </g>
          </g>
        ))}
      </g>
    </svg>
  );
};

export const BreakdownStages: React.FC = () => (
  <AbsoluteFill style={{ background: colors.background }}>
    <Stills />
    <StageBar />
  </AbsoluteFill>
);
