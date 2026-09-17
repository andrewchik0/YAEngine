import { interpolate } from 'remotion';
import { motion } from './tokens';

// 0..1 progress of an animation starting at `start` and lasting `duration` frames, linear
export const progress = (frame: number, start: number, duration: number) =>
  interpolate(frame, [start, start + duration], [0, 1], {
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
  });

export const mix = (from: number, to: number, t: number) => from + (to - from) * t;

// Standard enter: fade in while rising a few pixels
export const enterStyle = (frame: number, start: number, duration: number = motion.base, rise = 16) => {
  const t = progress(frame, start, duration);
  return { opacity: t, transform: `translateY(${mix(rise, 0, t)}px)` };
};

// Standard exit, mirrored enter
export const exitStyle = (frame: number, start: number, duration: number = motion.fast) => {
  const t = progress(frame, start, duration);
  return { opacity: 1 - t };
};
