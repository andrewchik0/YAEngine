import React from 'react';
import { AbsoluteFill, useCurrentFrame } from 'remotion';
import { enterStyle, exitStyle } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, motion, sec, type } from '../design/tokens';
import { credits, TODO } from '../shots/timeline';

export const creditsFrames = sec(credits.seconds);

// Unfinished credit data is painted in the warning color so it cannot ship unnoticed
const needsWork = (text: string) => text.includes(TODO) || text.includes('verify');

const Checked: React.FC<{ text: string; color: string }> = ({ text, color }) => (
  <span style={{ color: needsWork(text) ? colors.warning : color }}>{text}</span>
);

export const Credits: React.FC = () => {
  const frame = useCurrentFrame();
  const stagger = (i: number) => sec(0.15) + i * 5;

  return (
    <AbsoluteFill
      style={{
        background: colors.background,
        flexDirection: 'row',
        alignItems: 'center',
        padding: `0 ${layout.safeX * 2}px`,
        gap: 120,
        ...exitStyle(frame, creditsFrames - motion.slow, motion.slow),
      }}
    >
      <div style={{ flex: 1 }}>
        <div
          style={{
            fontFamily: fonts.sans,
            fontWeight: 700,
            fontSize: type.title + 16,
            color: colors.textPrimary,
            ...enterStyle(frame, stagger(0)),
          }}
        >
          {credits.name}
        </div>
        <div style={{ width: 120, height: 4, background: colors.accent, margin: '20px 0 28px', ...enterStyle(frame, stagger(1)) }} />
        {credits.links.map((link, i) => (
          <div
            key={link}
            style={{ fontFamily: fonts.mono, fontSize: type.body, marginBottom: 10, ...enterStyle(frame, stagger(2 + i)) }}
          >
            <Checked text={link} color={colors.textPrimary} />
          </div>
        ))}
        <div
          style={{
            fontFamily: fonts.sans,
            fontSize: type.label,
            color: colors.textTertiary,
            marginTop: 36,
            ...enterStyle(frame, stagger(4)),
          }}
        >
          {credits.tech}
        </div>
      </div>

      <div style={{ flex: 1 }}>
        <div
          style={{
            fontFamily: fonts.mono,
            fontSize: type.caption,
            letterSpacing: 2,
            textTransform: 'uppercase',
            color: colors.textTertiary,
            marginBottom: 22,
            ...enterStyle(frame, stagger(2)),
          }}
        >
          Third-party assets
        </div>
        {credits.attributions.map((a, i) => (
          <div
            key={a.what}
            style={{
              display: 'grid',
              gridTemplateColumns: '180px 1fr',
              columnGap: 24,
              padding: '12px 0',
              borderTop: `1px solid ${colors.border}`,
              fontFamily: fonts.sans,
              fontSize: type.label,
              ...enterStyle(frame, stagger(3 + i)),
            }}
          >
            <span style={{ color: colors.textSecondary }}>{a.what}</span>
            <span>
              <Checked text={a.credit} color={colors.textPrimary} />
              <span style={{ fontFamily: fonts.mono, fontSize: type.caption, marginLeft: 12 }}>
                <Checked text={a.license} color={colors.textTertiary} />
              </span>
            </span>
          </div>
        ))}
      </div>
    </AbsoluteFill>
  );
};
