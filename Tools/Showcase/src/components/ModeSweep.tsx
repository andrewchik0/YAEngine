import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { progress } from '../design/animate';
import { colors, layout, sec, VIDEO } from '../design/tokens';
import type { Stage } from '../shots/timeline';
import { Media } from './Media';
import { ModeLabel, ModeLabelAnchor } from './ModeLabel';

type ModeSweepProps = {
  stages: Stage[];
  holdSeconds: number;
  sweepSeconds: number;
};

export const modeSweepFrames = (stageCount: number, holdSeconds: number, sweepSeconds: number) =>
  sec(holdSeconds) * stageCount + sec(sweepSeconds) * (stageCount - 1);

// The camera is locked in every comparison recording, so the modes are pixel aligned and a sweep
// needs no matching. Each sweep crosses the whole frame - never a left half against a right half -
// and the same line cuts the label, so what is on screen is always named.
export const ModeSweep: React.FC<ModeSweepProps> = ({ stages, holdSeconds, sweepSeconds }) => {
  const frame = useCurrentFrame();
  const hold = sec(holdSeconds);
  const sweep = sec(sweepSeconds);

  // Sweep i reveals stage i + 1
  const sweepStart = (i: number) => hold * (i + 1) + sweep * i;
  const revealed = (i: number) => progress(frame, sweepStart(i), sweep);
  const running = stages.findIndex((_, i) => i < stages.length - 1 && revealed(i) > 0 && revealed(i) < 1);
  const linePercent = running === -1 ? undefined : revealed(running) * 100;

  return (
    <AbsoluteFill style={{ background: colors.background }}>
      {stages.map((stage, i) => (
        <AbsoluteFill
          key={stage.src}
          style={i === 0 ? undefined : { clipPath: `inset(0 ${100 - revealed(i - 1) * 100}% 0 0)` }}
        >
          <Media src={stage.src} label={`${stage.mode.title} - ${stage.src}`} trimBeforeSeconds={stage.trimBeforeSeconds} />
        </AbsoluteFill>
      ))}

      {linePercent === undefined ? null : (
        <div
          style={{
            position: 'absolute',
            top: 0,
            bottom: 0,
            left: `${linePercent}%`,
            width: layout.wipeLineWidth,
            marginLeft: -layout.wipeLineWidth / 2,
            background: colors.accent,
          }}
        />
      )}

      <ModeLabelAnchor>
        {stages.map((stage, i) => {
          // A label is only drawn between the line that revealed it and the line that covers
          // it, the same band its footage occupies. Its plate is translucent, so the labels
          // have to be cut apart instead of stacked.
          const from = i === stages.length - 1 ? 0 : revealed(i) * VIDEO.width;
          const to = i === 0 ? '0px' : `calc(100% - ${revealed(i - 1) * VIDEO.width}px)`;
          return (
            <div key={stage.mode.title} style={{ gridArea: '1 / 1', clipPath: `inset(0 ${to} 0 ${from}px)` }}>
              <ModeLabel mode={stage.mode} />
            </div>
          );
        })}
      </ModeLabelAnchor>
    </AbsoluteFill>
  );
};
