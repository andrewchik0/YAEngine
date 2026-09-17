import React from 'react';
import { modeSweepFrames, ModeSweep } from '../components/ModeSweep';
import { breakdownSweep } from '../shots/timeline';

const { stages, holdSeconds, sweepSeconds } = breakdownSweep;

export const breakdownSweepFrames = modeSweepFrames(stages.length, holdSeconds, sweepSeconds);

export const BreakdownSweep: React.FC = () => (
  <ModeSweep stages={stages} holdSeconds={holdSeconds} sweepSeconds={sweepSeconds} />
);
