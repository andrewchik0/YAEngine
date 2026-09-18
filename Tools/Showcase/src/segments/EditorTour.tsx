import React from 'react';
import { AbsoluteFill, Sequence, useCurrentFrame } from 'remotion';
import { Media } from '../components/Media';
import { AgentTerminal } from '../components/terminal/AgentTerminal';
import { compileTerminal } from '../components/terminal/compile';
import { mix, progress } from '../design/animate';
import { colors, layout, motion, VIDEO } from '../design/tokens';
import { editorTourScript } from '../shots/editorTourScript';
import { editorTour } from '../shots/timeline';

const terminal = compileTerminal(editorTourScript, 'editor-tour');

// The footage starts with the first step after the prompt, which is what the script's `at` times count from
const splitAt = terminal.firstStepAt;
export const editorTourFrames = splitAt + editorTour.footageFrames;

// Split layout: terminal docked left, editor footage right at 1:1, both the same height, centred together
const TERMINAL_W = 440;
const FOOTAGE_W = editorTour.crop.width;
const PANEL_H = editorTour.crop.height;
const PANEL_X = Math.round((VIDEO.width - TERMINAL_W - layout.gap - FOOTAGE_W) / 2);
const PANEL_Y = Math.round((VIDEO.height - PANEL_H) / 2);
const FOOTAGE_X = PANEL_X + TERMINAL_W + layout.gap;

// While the prompt is typed the terminal is centered, enlarged as far as the frame allows
const FULLSCREEN_SCALE = Math.min(1.45, (VIDEO.height - 2 * layout.safeY) / PANEL_H);
const DOCKED_CENTER_X = PANEL_X + TERMINAL_W / 2;

export const EditorTour: React.FC = () => {
  const frame = useCurrentFrame();
  const dock = progress(frame, splitAt, motion.slow);
  const footageIn = progress(frame, splitAt + motion.fast, motion.slow);

  return (
    <AbsoluteFill style={{ background: colors.background }}>
      <div
        style={{
          position: 'absolute',
          left: PANEL_X,
          top: PANEL_Y,
          transform: `translateX(${mix(VIDEO.width / 2 - DOCKED_CENTER_X, 0, dock)}px) scale(${mix(FULLSCREEN_SCALE, 1, dock)})`,
        }}
      >
        <AgentTerminal terminal={terminal} frame={frame} width={TERMINAL_W} height={PANEL_H} />
      </div>

      <div
        style={{
          position: 'absolute',
          left: FOOTAGE_X,
          top: PANEL_Y,
          width: FOOTAGE_W,
          height: PANEL_H,
          overflow: 'hidden',
          border: `1px solid ${colors.border}`,
          opacity: footageIn,
          transform: `translateX(${mix(80, 0, footageIn)}px)`,
        }}
      >
        <Sequence from={splitAt}>
          <Media
            src={editorTour.footage}
            trimBeforeSeconds={editorTour.trimBeforeSeconds}
            crop={editorTour.crop}
            label="Editor tour, driven by the agent"
            placeholderBrightness={0.25}
          />
        </Sequence>
      </div>
    </AbsoluteFill>
  );
};
