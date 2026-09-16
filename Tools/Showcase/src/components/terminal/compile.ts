import { random } from 'remotion';
import { sec } from '../../design/tokens';

export type TerminalStep =
  | { kind: 'prompt'; text: string }
  | { kind: 'thinking'; seconds: number }
  | { kind: 'say'; text: string }
  | { kind: 'tool'; name: string; detail: string; result: string; seconds: number };

// Every block carries absolute frames, so rendering a frame is a pure lookup
export type TerminalBlock =
  | { kind: 'user'; text: string; start: number }
  | { kind: 'thinking'; verb: string; start: number; end: number }
  | { kind: 'say'; text: string; start: number; revealFrames: number[] }
  | { kind: 'tool'; name: string; detail: string; result: string; start: number; resultAt: number };

export type CompiledTerminal = {
  prompt: string;
  // Frame at which each prompt character appears
  typedAt: number[];
  submitAt: number;
  blocks: TerminalBlock[];
  endAt: number;
};

const TYPING_LEAD_IN = sec(0.5);
const SUBMIT_PAUSE = sec(0.35);
const STEP_GAP = sec(0.25);

const THINKING_VERBS = ['Tracing rays', 'Sampling the BRDF', 'Reprojecting thoughts', 'Warming up caches'];

// Human typing: uneven per-key delays, longer after spaces, an occasional hesitation
const scheduleTyping = (text: string, start: number, seed: string) => {
  const typedAt: number[] = [];
  let frame = start;
  for (let i = 0; i < text.length; i++) {
    let delay = 3 + Math.floor(random(`${seed}-key-${i}`) * 4);
    if (text[i - 1] === ' ') delay += Math.floor(random(`${seed}-space-${i}`) * 4);
    if (random(`${seed}-hesitate-${i}`) < 0.08) delay += 8 + Math.floor(random(`${seed}-long-${i}`) * 6);
    frame += delay;
    typedAt.push(frame);
  }
  return typedAt;
};

// Streamed text arrives in uneven chunks, not per character
const scheduleReveal = (text: string, start: number, seed: string) => {
  const revealFrames: number[] = new Array(text.length);
  let frame = start;
  let index = 0;
  let chunk = 0;
  while (index < text.length) {
    const size = 4 + Math.floor(random(`${seed}-chunk-${chunk}`) * 9);
    for (let i = 0; i < size && index < text.length; i++, index++) revealFrames[index] = frame;
    frame += 2 + Math.floor(random(`${seed}-wait-${chunk}`) * 3);
    chunk++;
  }
  return revealFrames;
};

export const compileTerminal = (steps: TerminalStep[], seed = 'terminal'): CompiledTerminal => {
  const promptStep = steps.find((s) => s.kind === 'prompt');
  const prompt = promptStep?.kind === 'prompt' ? promptStep.text : '';
  const typedAt = scheduleTyping(prompt, TYPING_LEAD_IN, seed);
  const submitAt = (typedAt[typedAt.length - 1] ?? TYPING_LEAD_IN) + SUBMIT_PAUSE;

  const blocks: TerminalBlock[] = [];
  let cursor = submitAt;
  let thinkingIndex = 0;
  steps.forEach((step, index) => {
    switch (step.kind) {
      case 'prompt':
        blocks.push({ kind: 'user', text: step.text, start: submitAt });
        cursor = submitAt + STEP_GAP;
        break;
      case 'thinking': {
        const verb = THINKING_VERBS[thinkingIndex++ % THINKING_VERBS.length];
        blocks.push({ kind: 'thinking', verb, start: cursor, end: cursor + sec(step.seconds) });
        cursor += sec(step.seconds);
        break;
      }
      case 'say': {
        const revealFrames = scheduleReveal(step.text, cursor, `${seed}-say-${index}`);
        blocks.push({ kind: 'say', text: step.text, start: cursor, revealFrames });
        cursor = revealFrames[revealFrames.length - 1] + STEP_GAP;
        break;
      }
      case 'tool':
        blocks.push({
          kind: 'tool',
          name: step.name,
          detail: step.detail,
          result: step.result,
          start: cursor,
          resultAt: cursor + sec(step.seconds),
        });
        cursor += sec(step.seconds) + STEP_GAP;
        break;
    }
  });

  return { prompt, typedAt, submitAt, blocks, endAt: cursor };
};
