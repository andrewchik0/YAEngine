import React, { useMemo } from 'react';
import { AbsoluteFill, Sequence, useCurrentFrame } from 'remotion';
import { Media } from '../components/Media';
import { AgentTerminal } from '../components/terminal/AgentTerminal';
import { compileTerminal } from '../components/terminal/compile';
import { mix, progress } from '../design/animate';
import { colors, layout, motion, sec, VIDEO } from '../design/tokens';
import { editorTourScript } from '../shots/editorTourScript';
import { editorTour } from '../shots/timeline';

export const editorTourFrames = sec(editorTour.seconds);

// Split layout: terminal docked left, editor footage right, both the same height
const TERMINAL_W = 560;
const FOOTAGE_W = VIDEO.width - layout.safeX * 2 - layout.gap - TERMINAL_W;
const PANEL_H = Math.round((FOOTAGE_W * 9) / 16);
const PANEL_Y = Math.round((VIDEO.height - PANEL_H) / 2);
const FOOTAGE_X = layout.safeX + TERMINAL_W + layout.gap;

// While the prompt is typed the terminal is centered and enlarged
const FULLSCREEN_SCALE = 1.45;
const DOCKED_CENTER_X = layout.safeX + TERMINAL_W / 2;

export const EditorTour: React.FC = () => {
  const frame = useCurrentFrame();
  const terminal = useMemo(() => compileTerminal(editorTourScript, 'editor-tour'), []);
  const splitAt = terminal.submitAt + sec(0.25);
  const dock = progress(frame, splitAt, motion.slow);
  const footageIn = progress(frame, splitAt + motion.fast, motion.slow);

  return (
    <AbsoluteFill style={{ background: colors.background }}>
      <div
        style={{
          position: 'absolute',
          left: layout.safeX,
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
          <Media src={editorTour.footage} label="Editor tour, driven by the agent" placeholderBrightness={0.25} />
        </Sequence>
      </div>
    </AbsoluteFill>
  );
};
