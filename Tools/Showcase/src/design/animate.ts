import { interpolate } from 'remotion';
import { motion } from './tokens';

// 0..1 progress of an animation starting at `start` and lasting `duration` frames
export const progress = (frame: number, start: number, duration: number, easing = motion.out) =>
  interpolate(frame, [start, start + duration], [0, 1], {
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
    easing,
  });

export const mix = (from: number, to: number, t: number) => from + (to - from) * t;

// Standard enter: fade in while rising a few pixels
export const enterStyle = (frame: number, start: number, duration: number = motion.base, rise = 16) => {
  const t = progress(frame, start, duration);
  return { opacity: t, transform: `translateY(${mix(rise, 0, t)}px)` };
};

// Standard exit, mirrored enter
export const exitStyle = (frame: number, start: number, duration: number = motion.fast) => {
  const t = progress(frame, start, duration, motion.inOut);
  return { opacity: 1 - t };
};
