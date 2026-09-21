import React from 'react';
import { fonts } from '../design/fonts';
import { colors, type } from '../design/tokens';

// Chevrons of a breadcrumb bar: each one's left edge is notched by the point of the previous one,
// except for the first
export const CRUMB = {
  height: 60,
  point: 20,
  gap: 8,
  padX: 24,
  fontSize: type.label - 3,
};

let canvas: HTMLCanvasElement | undefined;

// System Arial is always there, so text can be measured synchronously
export const measureText = (text: string, size: number) => {
  canvas ??= document.createElement('canvas');
  const context = canvas.getContext('2d')!;
  context.font = `${size}px ${fonts.sans}`;
  return context.measureText(text).width;
};

// SVG points of a chevron: flat or notched left edge, pointed right edge
export const chevronPoints = (x: number, y: number, w: number, h: number, notch: number, point: number) =>
  [
    [x, y],
    [x + w - point, y],
    [x + w, y + h / 2],
    [x + w - point, y + h],
    [x, y + h],
    [x + notch, y + h / 2],
  ]
    .map(([px, py]) => `${px},${py}`)
    .join(' ');

// A crumb plate that turns lavender as `lit` goes to 1
export const Plate: React.FC<{ points: string; lit: number; outline: number }> = ({ points, lit, outline }) => (
  <>
    <polygon points={points} fill={colors.labelPlate} />
    <polygon points={points} fill="none" stroke={colors.textPrimary} strokeWidth={2} opacity={outline * (1 - lit)} />
    <polygon points={points} fill={colors.accent} stroke={colors.accent} strokeWidth={2} opacity={lit} />
  </>
);
