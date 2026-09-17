import React from 'react';
import { AbsoluteFill, Sequence, useCurrentFrame } from 'remotion';
import { LowerThird } from '../components/LowerThird';
import { hasStaticFile, Media } from '../components/Media';
import { enterStyle, mix, progress } from '../design/animate';
import { fonts } from '../design/fonts';
import { colors, layout, motion, sec, type, VIDEO } from '../design/tokens';
import { DOT, frameBreakdown, frameBreakdownSeconds, type MediaPath } from '../shots/timeline';

export const frameBreakdownFrames = sec(frameBreakdownSeconds);

const { layers, phases } = frameBreakdown;

// Phase boundaries in frames
const STRIP = sec(phases.strip);
const LAYOUT = sec(phases.layout);
const PER_LAYER = sec(phases.perLayer);
const COLLAPSE = sec(phases.collapse);
const LAYERS_START = STRIP + LAYOUT;
const LAYERS_END = LAYERS_START + PER_LAYER * layers.length;
const TONEMAP_START = LAYERS_END + COLLAPSE;

// Docked layout: the frame on the left, the 2x2 matrix on the right
const MATRIX_W = 520;
const CELL_GAP = 16;
const CELL_W = (MATRIX_W - CELL_GAP) / 2;
const CELL_H = Math.round((CELL_W * 9) / 16);
const HEADER_H = 40;
const ROW_LABEL_H = 34;
const CELL_TITLE_H = 34;
const ROW_H = ROW_LABEL_H + CELL_H + CELL_TITLE_H;
const ROW_GAP = 20;
const MATRIX_H = HEADER_H + ROW_H * 2 + ROW_GAP;
const MATRIX_X = VIDEO.width - layout.safeX - MATRIX_W;
const MATRIX_Y = Math.round((VIDEO.height - MATRIX_H) / 2);

type Rect = { x: number; y: number; w: number; h: number; r: number };

const FULL: Rect = { x: 0, y: 0, w: VIDEO.width, h: VIDEO.height, r: 0 };
const DOCKED: Rect = (() => {
  const w = MATRIX_X - layout.gap - layout.safeX;
  const h = Math.round((w * 9) / 16);
  return { x: layout.safeX, y: Math.round((VIDEO.height - h) / 2), w, h, r: layout.radius };
})();

const mixRect = (a: Rect, b: Rect, t: number): Rect => ({
  x: mix(a.x, b.x, t),
  y: mix(a.y, b.y, t),
  w: mix(a.w, b.w, t),
  h: mix(a.h, b.h, t),
  r: mix(a.r, b.r, t),
});

const cellRect = (row: number, column: number): Rect => ({
  x: MATRIX_X + column * (CELL_W + CELL_GAP),
  y: MATRIX_Y + HEADER_H + row * (ROW_H + ROW_GAP) + ROW_LABEL_H,
  w: CELL_W,
  h: CELL_H,
  r: 8,
});

const rectStyle = (rect: Rect): React.CSSProperties => ({
  position: 'absolute',
  left: rect.x,
  top: rect.y,
  width: rect.w,
  height: rect.h,
  borderRadius: rect.r,
  overflow: 'hidden',
});

// Placeholders get brighter with every layer so the build-up reads without real captures
const layerBrightness = (index: number) => 0.15 + index * 0.13;

type Align = 'center' | 'left' | 'right';
type FrameLayer = { src: MediaPath; label: string; brightness: number; reveal: number; opacity: number; align: Align };

const frameLayers = (frame: number): FrameLayer[] => {
  const direct = { src: frameBreakdown.direct, label: 'Direct lighting only', brightness: 0.03 };
  const final = { src: frameBreakdown.final, label: 'Final frame', brightness: 0.75 };

  if (frame < LAYERS_START) {
    const fade = progress(frame, Math.round(STRIP * 0.3), Math.round(STRIP * 0.5));
    return [
      { ...final, reveal: 1, opacity: 1, align: 'center' },
      { ...direct, reveal: 1, opacity: fade, align: 'center' },
    ];
  }

  if (frame < LAYERS_END) {
    const index = Math.floor((frame - LAYERS_START) / PER_LAYER);
    const local = frame - LAYERS_START - index * PER_LAYER;
    const layer = layers[index];
    const previous =
      index === 0
        ? direct
        : { src: layers[index - 1].result, label: layers[index - 1].title, brightness: layerBrightness(index - 1) };
    const wipe = progress(local, Math.round(PER_LAYER * 0.3), Math.round(PER_LAYER * 0.25));
    return [
      { ...previous, reveal: 1, opacity: 1, align: 'right' },
      {
        src: layer.result,
        label: `+ ${layer.title}`,
        brightness: layerBrightness(index),
        reveal: wipe,
        opacity: 1,
        align: 'left',
      },
    ];
  }

  const last = layers[layers.length - 1];
  const allLayers = { src: last.result, label: 'All layers', brightness: layerBrightness(layers.length - 1) };
  if (frame < TONEMAP_START) return [{ ...allLayers, reveal: 1, opacity: 1, align: 'center' }];

  const tonemapFrames = sec(phases.tonemap);
  const local = frame - TONEMAP_START;
  const wipe = progress(local, Math.round(tonemapFrames * 0.3), Math.round(tonemapFrames * 0.3));
  return [
    {
      src: frameBreakdown.tonemap.linear,
      label: 'Linear HDR, no tonemapping',
      brightness: 0.3,
      reveal: 1,
      opacity: 1,
      align: 'right',
    },
    { ...final, reveal: wipe, opacity: 1, align: 'left' },
  ];
};

// Real diff images are tinted with the accent; without one, a soft glow marks the moment
const DiffFlash: React.FC<{ src: MediaPath; local: number }> = ({ src, local }) => {
  const up = progress(local, 0, Math.round(PER_LAYER * 0.1));
  const down = progress(local, Math.round(PER_LAYER * 0.12), Math.round(PER_LAYER * 0.2));
  return (
    <AbsoluteFill style={{ opacity: up * (1 - down), mixBlendMode: 'screen' }}>
      {hasStaticFile(src) ? (
        <Media src={src} style={{ filter: 'sepia(1) saturate(5) hue-rotate(-25deg)' }} />
      ) : (
        <AbsoluteFill
          style={{ background: `radial-gradient(ellipse at 50% 55%, ${colors.accent} 0%, transparent 60%)`, opacity: 0.55 }}
        />
      )}
    </AbsoluteFill>
  );
};

const Cell: React.FC<{ index: number; frame: number }> = ({ index, frame }) => {
  const layer = layers[index];
  const rect = cellRect(layer.row, layer.column);
  const start = LAYERS_START + index * PER_LAYER;
  const active = frame >= start && frame < start + PER_LAYER;
  const done = frame >= start + PER_LAYER;
  const highlight = progress(frame, start, motion.fast);
  const appear = enterStyle(frame, STRIP + Math.round(LAYOUT * 0.3) + index * 4);

  return (
    <div style={{ ...appear }}>
      <div
        style={{
          ...rectStyle(rect),
          border: `2px solid ${active ? colors.accent : colors.border}`,
          transform: `scale(${active ? mix(1, 1.04, highlight) : 1})`,
          opacity: active || done ? 1 : 0.4,
          filter: active || done ? 'none' : 'grayscale(1)',
        }}
      >
        <Media src={layer.debug} label={layer.title} placeholderBrightness={layerBrightness(index)} />
      </div>
      <div
        style={{
          position: 'absolute',
          left: rect.x,
          top: rect.y + rect.h + 8,
          width: rect.w,
          fontFamily: fonts.sans,
          fontWeight: 500,
          fontSize: type.label,
          color: active ? colors.textPrimary : done ? colors.textSecondary : colors.textTertiary,
        }}
      >
        {layer.title}
      </div>
    </div>
  );
};

const MatrixLabels: React.FC<{ frame: number }> = ({ frame }) => {
  const appear = enterStyle(frame, STRIP + Math.round(LAYOUT * 0.2));
  const label: React.CSSProperties = {
    position: 'absolute',
    fontFamily: fonts.mono,
    fontSize: type.caption,
    letterSpacing: 2,
    textTransform: 'uppercase',
  };
  return (
    <div style={appear}>
      {frameBreakdown.columns.map((column, c) => (
        <div
          key={column}
          style={{ ...label, left: MATRIX_X + c * (CELL_W + CELL_GAP), top: MATRIX_Y, color: colors.textTertiary }}
        >
          {column}
        </div>
      ))}
      {frameBreakdown.rows.map((row, r) => (
        <div
          key={row}
          style={{
            ...label,
            left: MATRIX_X,
            top: MATRIX_Y + HEADER_H + r * (ROW_H + ROW_GAP),
            color: colors.accent,
          }}
        >
          {row}
        </div>
      ))}
    </div>
  );
};

const StepTitle: React.FC<{ frame: number }> = ({ frame }) => {
  const index = Math.floor((frame - LAYERS_START) / PER_LAYER);
  const layer = layers[index];
  const local = frame - LAYERS_START - index * PER_LAYER;
  return (
    <div style={{ position: 'absolute', left: DOCKED.x, top: DOCKED.y - 92, ...enterStyle(local, 0) }}>
      <span style={{ fontFamily: fonts.sans, fontWeight: 700, fontSize: type.heading + 8, color: colors.textPrimary }}>
        {`+ ${layer.title}`}
      </span>
      <span style={{ fontFamily: fonts.mono, fontSize: type.label, color: colors.textSecondary, marginLeft: 18 }}>
        {`${frameBreakdown.rows[layer.row]}${DOT}${frameBreakdown.columns[layer.column]}`}
      </span>
    </div>
  );
};

export const FrameBreakdown: React.FC = () => {
  const frame = useCurrentFrame();

  const dock = progress(frame, STRIP, LAYOUT);
  const undock = progress(frame, LAYERS_END, COLLAPSE);
  const frameRect = mixRect(FULL, DOCKED, dock * (1 - undock));
  const matrixVisible = frame >= STRIP && frame < LAYERS_END + COLLAPSE;
  const matrixOpacity = 1 - undock;

  const inLayers = frame >= LAYERS_START && frame < LAYERS_END;
  const layerIndex = Math.floor((frame - LAYERS_START) / PER_LAYER);
  const layerLocal = frame - LAYERS_START - layerIndex * PER_LAYER;

  return (
    <AbsoluteFill style={{ background: colors.background }}>
      <div style={{ ...rectStyle(frameRect), boxShadow: dock > 0 ? '0 30px 80px rgba(0, 0, 0, 0.5)' : 'none' }}>
        {frameLayers(frame).map((layer, i) => (
          <AbsoluteFill
            key={i}
            style={{ opacity: layer.opacity, clipPath: `inset(0 ${(1 - layer.reveal) * 100}% 0 0)` }}
          >
            <Media
              src={layer.src}
              label={layer.label}
              placeholderBrightness={layer.brightness}
              placeholderAlign={layer.align}
            />
          </AbsoluteFill>
        ))}
        {inLayers ? <DiffFlash src={layers[layerIndex].diff} local={layerLocal - Math.round(PER_LAYER * 0.5)} /> : null}
      </div>

      {matrixVisible ? (
        <AbsoluteFill style={{ opacity: matrixOpacity }}>
          <MatrixLabels frame={frame} />
          {layers.map((_, index) => (
            <Cell key={index} index={index} frame={frame} />
          ))}
        </AbsoluteFill>
      ) : null}

      {inLayers ? <FlyingThumbnail index={layerIndex} local={layerLocal} /> : null}
      {inLayers ? <StepTitle frame={frame} /> : null}

      {frame < STRIP ? (
        <Sequence from={Math.round(STRIP * 0.5)} layout="none">
          <LowerThird title="Direct lighting" detail="indirect light removed" />
        </Sequence>
      ) : null}
      {frame >= TONEMAP_START ? (
        <Sequence from={TONEMAP_START} layout="none">
          <LowerThird title={frameBreakdown.tonemap.title} detail={frameBreakdown.tonemap.detail} />
        </Sequence>
      ) : null}
    </AbsoluteFill>
  );
};

// The technique's debug view leaves its cell and dissolves into the frame
const FlyingThumbnail: React.FC<{ index: number; local: number }> = ({ index, local }) => {
  const layer = layers[index];
  const t = progress(local, Math.round(PER_LAYER * 0.1), Math.round(PER_LAYER * 0.3));
  if (t <= 0 || t >= 1) return null;
  const rect = mixRect(cellRect(layer.row, layer.column), DOCKED, t);
  return (
    <div style={{ ...rectStyle(rect), opacity: 1 - t * t, border: `2px solid ${colors.accent}` }}>
      <Media src={layer.debug} label={layer.title} placeholderBrightness={layerBrightness(index)} />
    </div>
  );
};
