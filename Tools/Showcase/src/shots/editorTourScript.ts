import type { TerminalStep } from '../components/terminal/compile';

// Re-enacts the run in footage/editor_tour.mp4. Every call is one the agent made over the bridge:
// editor_batch applies all its steps between two frames, ui_do works a widget. A tool's result
// lands (at + seconds) on the frame its effect appears in the footage; the times were measured on
// the take. The calls that add objects work for about 1.4s, as the run paused before each of them.
// Tab clicks back to the Outliner are left out.
export const editorTourScript: TerminalStep[] = [
  { kind: 'prompt', text: 'build a car studio and show off the editor' },
  { kind: 'thinking', seconds: 0.8 },
  { kind: 'say', text: 'Empty scene. Light first, so nothing appears in the dark.', at: 0.95 },
  {
    kind: 'tool', name: 'editor_batch', detail: 'DLAA, fog and dither off, camera',
    result: '6 steps, one frame', at: 1.7, seconds: 0.35,
  },
  {
    kind: 'tool', name: 'editor_batch', detail: 'floor, 2 softboxes, 3 lights',
    result: '19 steps, one frame', at: 2.55, seconds: 1.42,
  },
  {
    kind: 'tool', name: 'ui_do', detail: 'Details',
    result: 'Softbox_R_Light', at: 4.7, seconds: 0.28,
  },
  {
    kind: 'tool', name: 'editor_batch', detail: 'lavender back softbox',
    result: '7 steps, one frame', at: 6.5, seconds: 1.42,
  },
  {
    kind: 'tool', name: 'ui_do', detail: 'Details',
    result: 'Softbox_Back_Light', at: 8.65, seconds: 0.3,
  },
  { kind: 'say', text: 'Now the car, as the Bistro scene has it, minus its drive.', at: 9.6 },
  {
    kind: 'tool', name: 'editor_batch', detail: 'importFromScene porsche_studio.scene',
    result: 'car, 4 wheel probes, no motion path', at: 11.05, seconds: 1.43,
  },
  {
    kind: 'tool', name: 'ui_do', detail: 'Details',
    result: 'black_porsche.glb', at: 13.2, seconds: 0.3,
  },
  { kind: 'say', text: 'Something to reflect: metal and gloss, out of the floor.', at: 14.1 },
  {
    kind: 'tool', name: 'editor_batch', detail: '2 pillars below the floor',
    result: '7 steps, one frame', at: 15.05, seconds: 1.42,
  },
  {
    kind: 'tool', name: 'editor_batch', detail: 'position.y, one per frame',
    result: 'pillars up', at: 17.0, seconds: 1.32,
  },
  {
    kind: 'tool', name: 'editor_batch', detail: '3 spheres below the floor',
    result: '10 steps, one frame', at: 20.35, seconds: 1.43,
  },
  {
    kind: 'tool', name: 'editor_batch', detail: 'position.y, one per frame',
    result: 'spheres up', at: 22.3, seconds: 1.6,
  },
  { kind: 'say', text: 'Last, a shot round the car. The gold sphere changes while it plays.', at: 24.6 },
  {
    kind: 'tool', name: 'editor_batch', detail: 'Orbit shot, 7 keys round the car centre',
    result: 'Shot 1, 6 s', at: 26.1, seconds: 1.38,
  },
  {
    kind: 'tool', name: 'editor_batch', detail: 'pilot, play, gizmos off',
    result: 'playing Shot 1', at: 28.0, seconds: 0.48,
  },
  {
    kind: 'tool', name: 'editor_batch', detail: 'Sphere_Gold albedo, every 50 ms',
    result: 'gold, lavender, cyan, gold', at: 28.9, seconds: 5.4,
  },
];
