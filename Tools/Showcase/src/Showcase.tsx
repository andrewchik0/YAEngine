import { fade } from '@remotion/transitions/fade';
import { linearTiming, TransitionSeries } from '@remotion/transitions';
import React from 'react';
import { AbsoluteFill } from 'remotion';
import { colors, motion } from './design/tokens';
import { BreakdownStages, breakdownStagesFrames } from './segments/BreakdownStages';
import { Credits, creditsFrames } from './segments/Credits';
import { EditorTour, editorTourFrames } from './segments/EditorTour';
import { Flythrough, flythroughFrames } from './segments/Flythrough';
import { Intro, introFrames } from './segments/Intro';
import { RasterVsPathTracing, rasterVsPathTracingFrames } from './segments/RasterVsPathTracing';
import { TitleCard, titleCardFrames } from './segments/TitleCard';
import { titleCards } from './shots/timeline';

const TitlePathTracing: React.FC = () => <TitleCard card={titleCards.pathTracing} />;
const TitleBreakdown: React.FC = () => <TitleCard card={titleCards.breakdown} />;
const TitleEditor: React.FC = () => <TitleCard card={titleCards.editor} />;

// FrameBreakdown, the matrix animatic, is still a composition of its own but is not in the
// edit: the breakdown is now the stage by stage stills of BreakdownStages.
export const SEGMENTS = [
  { id: 'Intro', frames: introFrames, Component: Intro },
  { id: 'Flythrough', frames: flythroughFrames, Component: Flythrough },
  { id: 'TitlePathTracing', frames: titleCardFrames(titleCards.pathTracing), Component: TitlePathTracing },
  { id: 'RasterVsPathTracing', frames: rasterVsPathTracingFrames, Component: RasterVsPathTracing },
  { id: 'TitleBreakdown', frames: titleCardFrames(titleCards.breakdown), Component: TitleBreakdown },
  { id: 'BreakdownStages', frames: breakdownStagesFrames, Component: BreakdownStages },
  { id: 'TitleEditor', frames: titleCardFrames(titleCards.editor), Component: TitleEditor },
  { id: 'EditorTour', frames: editorTourFrames, Component: EditorTour },
  { id: 'Credits', frames: creditsFrames, Component: Credits },
] as const;

type Segment = (typeof SEGMENTS)[number];

// The segments that already have real footage, so the edit can be watched end to end
// while the rest is still being captured
export const PREVIEW_SEGMENTS = SEGMENTS.filter(({ id }) =>
  ['Intro', 'Flythrough', 'TitlePathTracing', 'RasterVsPathTracing', 'TitleBreakdown', 'BreakdownStages'].includes(id),
);

// Each transition overlaps two neighbours, so it shortens the video by its own length
export const framesOf = (segments: readonly Segment[]) =>
  segments.reduce((sum, s) => sum + s.frames, 0) - (segments.length - 1) * motion.segmentTransition;

export const SHOWCASE_FRAMES = framesOf(SEGMENTS);
export const PREVIEW_FRAMES = framesOf(PREVIEW_SEGMENTS);

const Cut: React.FC<{ segments: readonly Segment[] }> = ({ segments }) => (
  <AbsoluteFill style={{ background: colors.background }}>
    <TransitionSeries>
      {segments.flatMap(({ id, frames, Component }, index) => {
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

export const Showcase: React.FC = () => <Cut segments={SEGMENTS} />;

export const ShowcasePreview: React.FC = () => <Cut segments={PREVIEW_SEGMENTS} />;
