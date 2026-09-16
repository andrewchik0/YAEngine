import type { TerminalStep } from '../components/terminal/compile';

// Hand-written for now; later generated from the logs of a real agent run
export const editorTourScript: TerminalStep[] = [
  { kind: 'prompt', text: 'show off editor functionality' },
  { kind: 'thinking', seconds: 1.2 },
  { kind: 'say', text: 'Quick tour of the viewport. Watch the cube.' },
  { kind: 'tool', name: 'entity.createPrimitive', detail: 'cube at (0, 0.5, 0)', result: 'Created "Cube"', seconds: 0.6 },
  { kind: 'tool', name: 'ui.drag', detail: 'gizmo X +2.0 m', result: 'Cube moved', seconds: 1.6 },
  { kind: 'thinking', seconds: 0.7 },
  { kind: 'tool', name: 'ui.drag', detail: 'Inspector/Roughness 0.05 -> 0.90', result: 'Material updated', seconds: 2.0 },
  { kind: 'tool', name: 'view.setDebugView', detail: 'normals', result: 'View: world normals', seconds: 1.2 },
  { kind: 'tool', name: 'view.setDebugView', detail: 'lit', result: 'View: lit', seconds: 0.4 },
  { kind: 'thinking', seconds: 0.8 },
  { kind: 'tool', name: 'entity.create', detail: 'ReflectionProbe above the cube', result: 'Created "ReflectionProbe"', seconds: 0.5 },
  { kind: 'tool', name: 'bake.probe', detail: 'ReflectionProbe', result: 'Baked in 1.8 s', seconds: 2.2 },
  { kind: 'say', text: 'Done. The cube now reflects the street around it.' },
];
