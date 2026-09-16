import { fade } from '@remotion/transitions/fade';
import { linearTiming, TransitionSeries } from '@remotion/transitions';
import React from 'react';
import { AbsoluteFill } from 'remotion';
import { colors, motion } from './design/tokens';
import { Credits, creditsFrames } from './segments/Credits';
import { EditorTour, editorTourFrames } from './segments/EditorTour';
import { Flythrough, flythroughFrames } from './segments/Flythrough';
import { FrameBreakdown, frameBreakdownFrames } from './segments/FrameBreakdown';
import { Intro, introFrames } from './segments/Intro';
import { RasterVsPathTracing, rasterVsPathTracingFrames } from './segments/RasterVsPathTracing';

export const SEGMENTS = [
  { id: 'Intro', frames: introFrames, Component: Intro },
  { id: 'Flythrough', frames: flythroughFrames, Component: Flythrough },
  { id: 'RasterVsPathTracing', frames: rasterVsPathTracingFrames, Component: RasterVsPathTracing },
  { id: 'FrameBreakdown', frames: frameBreakdownFrames, Component: FrameBreakdown },
  { id: 'EditorTour', frames: editorTourFrames, Component: EditorTour },
  { id: 'Credits', frames: creditsFrames, Component: Credits },
] as const;

// Each transition overlaps two neighbours, so it shortens the video by its own length
export const SHOWCASE_FRAMES =
  SEGMENTS.reduce((sum, s) => sum + s.frames, 0) - (SEGMENTS.length - 1) * motion.segmentTransition;

export const Showcase: React.FC = () => (
  <AbsoluteFill style={{ background: colors.background }}>
    <TransitionSeries>
      {SEGMENTS.flatMap(({ id, frames, Component }, index) => {
        const sequence = (
          <TransitionSeries.Sequence key={id} durationInFrames={frames}>
            <Component />
          </TransitionSeries.Sequence>
        );
        if (index === 0) return [sequence];
        return [
          <TransitionSeries.Transition
            key={`${id}-in`}
            presentation={fade()}
            timing={linearTiming({ durationInFrames: motion.segmentTransition })}
          />,
          sequence,
        ];
      })}
    </TransitionSeries>
  </AbsoluteFill>
);
