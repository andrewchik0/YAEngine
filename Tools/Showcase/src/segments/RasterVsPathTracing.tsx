import React from 'react';
import { Sequence, Series } from 'remotion';
import { Callout } from '../components/Callout';
import { modeSweepFrames, ModeSweep } from '../components/ModeSweep';
import { sec } from '../design/tokens';
import { rasterVsPathTracing } from '../shots/timeline';

const { comparisons, holdSeconds, sweepSeconds } = rasterVsPathTracing;

const framesOf = (stageCount: number) => modeSweepFrames(stageCount, holdSeconds, sweepSeconds);

export const rasterVsPathTracingFrames = comparisons.reduce((sum, c) => sum + framesOf(c.stages.length), 0);

export const RasterVsPathTracing: React.FC = () => (
  <Series>
    {comparisons.map(({ name, stages, callout }) => (
      <Series.Sequence key={name} durationInFrames={framesOf(stages.length)}>
        <ModeSweep stages={stages} holdSeconds={holdSeconds} sweepSeconds={sweepSeconds} />
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
    ))}
  </Series>
);
