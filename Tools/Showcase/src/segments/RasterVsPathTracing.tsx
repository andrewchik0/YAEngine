import React from 'react';
import { AbsoluteFill, interpolate, Sequence, Series, useCurrentFrame } from 'remotion';
import { ABWipe } from '../components/ABWipe';
import { Callout } from '../components/Callout';
import { LowerThird } from '../components/LowerThird';
import { Media } from '../components/Media';
import { ModeLabel } from '../components/ModeLabel';
import { colors, motion, sec } from '../design/tokens';
import { MODES, rasterVsPathTracing } from '../shots/timeline';

const { comparisons, glass } = rasterVsPathTracing;

export const rasterVsPathTracingFrames =
  comparisons.reduce((sum, c) => sum + sec(c.seconds), 0) + sec(glass.seconds);

// Path traced stills are accumulated offline, so a slow push-in stands in for camera motion
const GlassShot: React.FC<{ durationInFrames: number }> = ({ durationInFrames }) => {
  const frame = useCurrentFrame();
  const scale = interpolate(frame, [0, durationInFrames], [1, 1.06]);
  return (
    <AbsoluteFill style={{ background: colors.background }}>
      <AbsoluteFill style={{ transform: `scale(${scale})` }}>
        <Media src={glass.still} label="Path traced glass, accumulated" placeholderBrightness={0.5} />
      </AbsoluteFill>
      <ModeLabel mode={MODES.pathTracer} />
      <Sequence from={motion.base}>
        <LowerThird title={glass.title} detail={glass.aside} italicDetail detailDelay={sec(0.8)} />
      </Sequence>
    </AbsoluteFill>
  );
};

export const RasterVsPathTracing: React.FC = () => (
  <Series>
    {comparisons.map((comparison) => {
      const frames = sec(comparison.seconds);
      const callout = comparison.callout;
      return (
        <Series.Sequence key={comparison.name} durationInFrames={frames}>
          <ABWipe
            a={comparison.raster}
            b={comparison.pathTraced}
            aMode={MODES.raster}
            bMode={MODES.pathTracer}
            durationInFrames={frames}
            label={comparison.name}
          />
          {callout ? (
            <Sequence from={sec(callout.atSeconds)} durationInFrames={sec(callout.seconds)}>
              <Callout
                x={callout.x}
                y={callout.y}
                title={callout.title}
                detail={callout.detail}
                durationInFrames={sec(callout.seconds)}
              />
            </Sequence>
          ) : null}
        </Series.Sequence>
      );
    })}
    <Series.Sequence durationInFrames={sec(glass.seconds)}>
      <GlassShot durationInFrames={sec(glass.seconds)} />
    </Series.Sequence>
  </Series>
);
