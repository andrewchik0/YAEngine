import React from 'react';
import { fonts } from '../../design/fonts';
import { colors, layout, VIDEO } from '../../design/tokens';
import type { CompiledTerminal, TerminalBlock } from './compile';

// Terminal UIs redraw in discrete steps: nothing in here is eased, only counted in frames
const SPINNER = ['\u280b', '\u2819', '\u2839', '\u2838', '\u283c', '\u2834', '\u2826', '\u2827', '\u2807', '\u280f'];
const SPINNER_FRAMES_PER_GLYPH = 6;
const CARET_BLINK_FRAMES = 32;
const TOOL_BLINK_FRAMES = 18;
const BULLET = '\u25cf';
const RESULT_BRANCH = '\u2514';
const ELLIPSIS = '\u2026';

const FONT_SIZE = 19;
const LINE_HEIGHT = 1.45;

const countUpTo = (frames: number[], frame: number) => {
  let count = 0;
  while (count < frames.length && frames[count] <= frame) count++;
  return count;
};

const Line: React.FC<{ children: React.ReactNode; style?: React.CSSProperties }> = ({ children, style }) => (
  <div style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word', ...style }}>{children}</div>
);

const Bullet: React.FC<{ color: string }> = ({ color }) => <span style={{ color }}>{`${BULLET} `}</span>;

const Block: React.FC<{ block: TerminalBlock; frame: number }> = ({ block, frame }) => {
  switch (block.kind) {
    case 'user':
      return (
        <Line style={{ background: 'rgba(255, 255, 255, 0.05)', padding: '6px 10px', borderRadius: 6 }}>
          <span style={{ color: colors.textTertiary }}>{'> '}</span>
          <span style={{ color: colors.textPrimary }}>{block.text}</span>
        </Line>
      );
    case 'thinking': {
      if (frame >= block.end) return null;
      const elapsed = frame - block.start;
      const glyph = SPINNER[Math.floor(elapsed / SPINNER_FRAMES_PER_GLYPH) % SPINNER.length];
      return (
        <Line>
          <span style={{ color: colors.accent }}>{`${glyph} `}</span>
          <span style={{ color: colors.accent }}>{`${block.verb}${ELLIPSIS}`}</span>
          <span style={{ color: colors.textTertiary }}>{` (${Math.floor(elapsed / VIDEO.fps)}s)`}</span>
        </Line>
      );
    }
    case 'say':
      return (
        <Line>
          <Bullet color={colors.textPrimary} />
          <span style={{ color: colors.textPrimary }}>{block.text.slice(0, countUpTo(block.revealFrames, frame))}</span>
        </Line>
      );
    case 'tool': {
      const done = frame >= block.resultAt;
      const blinkOn = Math.floor((frame - block.start) / TOOL_BLINK_FRAMES) % 2 === 0;
      const bulletColor = done ? colors.success : blinkOn ? colors.textPrimary : colors.textTertiary;
      return (
        <div>
          <Line>
            <Bullet color={bulletColor} />
            <span style={{ color: colors.textPrimary, fontWeight: 700 }}>{block.name}</span>
            <span style={{ color: colors.textSecondary }}>{`(${block.detail})`}</span>
          </Line>
          {done ? (
            <Line style={{ color: colors.textTertiary, paddingLeft: '1.2em' }}>{`${RESULT_BRANCH} ${block.result}`}</Line>
          ) : null}
        </div>
      );
    }
  }
};

type AgentTerminalProps = {
  terminal: CompiledTerminal;
  frame: number;
  width: number;
  height: number;
};

export const AgentTerminal: React.FC<AgentTerminalProps> = ({ terminal, frame, width, height }) => {
  const submitted = frame >= terminal.submitAt;
  const typed = submitted ? '' : terminal.prompt.slice(0, countUpTo(terminal.typedAt, frame));
  const lastKey = terminal.typedAt[terminal.typedAt.length - 1] ?? 0;
  const typing = !submitted && frame >= (terminal.typedAt[0] ?? 0) - 4 && frame <= lastKey + 4;
  const caretOn = typing || Math.floor(frame / CARET_BLINK_FRAMES) % 2 === 0;
  const visible = terminal.blocks.filter((block) => block.start <= frame);

  return (
    <div
      style={{
        width,
        height,
        display: 'flex',
        flexDirection: 'column',
        background: colors.surface,
        border: `1px solid ${colors.borderStrong}`,
        borderRadius: layout.radius,
        overflow: 'hidden',
        boxShadow: '0 30px 80px rgba(0, 0, 0, 0.5)',
        fontFamily: fonts.mono,
        fontSize: FONT_SIZE,
        lineHeight: LINE_HEIGHT,
      }}
    >
      <div
        style={{
          height: 44,
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          padding: '0 18px',
          background: colors.surfaceRaised,
          borderBottom: `1px solid ${colors.border}`,
          fontSize: 15,
        }}
      >
        <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
          <span style={{ color: colors.accent, fontWeight: 700 }}>yae</span>
          <span style={{ color: colors.textSecondary }}>agent</span>
        </div>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center', color: colors.textTertiary }}>
          <span style={{ color: colors.success }}>{BULLET}</span>
          <span>YAEngine MCP</span>
        </div>
      </div>

      <div
        style={{
          flex: 1,
          display: 'flex',
          flexDirection: 'column',
          justifyContent: 'flex-end',
          gap: 12,
          padding: '18px 20px',
          overflow: 'hidden',
        }}
      >
        {visible.map((block, index) => (
          <Block key={index} block={block} frame={frame} />
        ))}
      </div>

      <div style={{ padding: '0 16px 12px', flexShrink: 0 }}>
        <div
          style={{
            border: `1px solid ${colors.borderStrong}`,
            borderRadius: 10,
            padding: '10px 14px',
            minHeight: FONT_SIZE * LINE_HEIGHT,
            whiteSpace: 'pre-wrap',
          }}
        >
          <span style={{ color: colors.textTertiary }}>{'> '}</span>
          <span style={{ color: colors.textPrimary }}>{typed}</span>
          <span
            style={{
              display: 'inline-block',
              width: '0.6em',
              height: '1.15em',
              verticalAlign: 'text-bottom',
              background: caretOn ? colors.textPrimary : 'transparent',
            }}
          />
        </div>
        <div style={{ marginTop: 8, fontSize: 14, color: colors.textTertiary, paddingLeft: 4 }}>
          ReleaseEditor {'\u00b7'} 5 tool groups {'\u00b7'} scene cafe_merged
        </div>
      </div>
    </div>
  );
};
