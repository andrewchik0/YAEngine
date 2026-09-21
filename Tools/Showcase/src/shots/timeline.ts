// The whole edit as data. Paths are relative to public/; a file that does not exist yet
// renders as a labeled placeholder, so the animatic always plays end to end.

export type MediaPath = string;

export type Clip = {
  src: MediaPath;
  seconds: number;
  label?: string;
  trimBeforeSeconds?: number;
};

export type Mode = {
  title: string;
  detail: string;
};

export type Point = { x: number; y: number };

export type CalloutSpec = {
  atSeconds: number;
  seconds: number;
  // Target point in output pixels
  x: number;
  y: number;
  title: string;
  detail: string;
  // Top-right corner of the text and the arrow's bend, when up and to the left of the target
  // is not a dark enough place for the text
  label?: Point;
  bend?: Point;
};

export const TODO = 'TODO';

export const DOT = ' \u00b7 ';

// fps is not repeated here: the PresentMon overlay is burned into every recording
export const MODES = {
  raster: { title: 'Rasterizer', detail: 'TAA' },
  pathTracer: { title: 'Path tracer', detail: 'DLSS Balanced' },
  pathTracerGlass: { title: 'Path tracer + glass', detail: 'DLSS Balanced' },
} satisfies Record<string, Mode>;

export const intro = {
  seconds: 4,
  title: 'YAEngine',
  author: 'Andrei Vasilev',
  capture: 'captured on an RTX 4060 Ti 8GB in 1080p',
};

export type TitleCardSpec = {
  seconds: number;
  title: string;
  detail?: string;
};

// Statements between the parts, set like the opening card
export const titleCards = {
  pathTracing: {
    seconds: 3.5,
    title: 'It path traces, too!',
  },
  breakdown: {
    seconds: 3.5,
    title: 'How each frame is composed',
    detail: 'rasterizer render pipeline, layer by layer',
  },
  editor: {
    seconds: 3.5,
    title: 'It has an editor',
    detail: 'The tour is driven by an AI agent over MCP',
  },
} satisfies Record<string, TitleCardSpec>;

export const flythrough = {
  mode: MODES.raster,
  // The camera starts moving on frame 500 of the recording and jumps to the next sequencer shot
  // on frame 2028; the clip ends on the frame before the jump, or the cut into the next card
  // catches the teleport
  clips: [{ src: 'footage/intro.mp4', seconds: (2028 - 500) / 60, trimBeforeSeconds: 500 / 60 }] as Clip[],
};

// One rendering mode of one comparison shot
export type Stage = {
  mode: Mode;
  src: MediaPath;
  // Into the recording. Every comparison plays the same 15 s sequencer shot in each mode, so the
  // trim is the frame its camera starts moving, nudged by a frame where matching the edges of
  // the two modes says so: the same output frame then shows the same camera.
  trimBeforeSeconds?: number;
};

export type Comparison = {
  name: string;
  stages: Stage[];
  // Drawn over the sweep; its time is counted from the start of the comparison
  callout?: CalloutSpec;
};

export const rasterVsPathTracing = {
  // Each mode stands still this long, then the line crosses the frame in this long
  holdSeconds: 2.6,
  sweepSeconds: 2.6,
  comparisons: [
    {
      name: 'Exterior, rear',
      stages: [
        { mode: MODES.raster, src: 'footage/raster0.mp4', trimBeforeSeconds: 241 / 60 },
        { mode: MODES.pathTracer, src: 'footage/pt0.mp4', trimBeforeSeconds: 448 / 60 },
      ],
      // The glossy rear fender, which drifts from (1130, 450) to (1136, 468) while the callout is
      // up. Above it are the wing's grey end plate and the white 33, so the text goes under the
      // tail light, on the black lower bumper.
      callout: {
        atSeconds: 4.2,
        seconds: 3,
        x: 1133,
        y: 459,
        title: 'Clear coat',
        detail: 'two-layer material',
        label: { x: 1060, y: 660 },
        bend: { x: 1150, y: 660 },
      },
    },
    {
      name: 'Exterior, front',
      stages: [
        { mode: MODES.raster, src: 'footage/raster1.mp4', trimBeforeSeconds: 203 / 60 },
        { mode: MODES.pathTracer, src: 'footage/pt1.mp4', trimBeforeSeconds: 962 / 60 },
      ],
    },
    {
      name: 'Interior',
      stages: [
        { mode: MODES.raster, src: 'footage/raster2.mp4', trimBeforeSeconds: 408 / 60 },
        { mode: MODES.pathTracer, src: 'footage/pt2.mp4', trimBeforeSeconds: 728 / 60 },
        { mode: MODES.pathTracerGlass, src: 'footage/pt2_real_glass.mp4', trimBeforeSeconds: 1804 / 60 },
      ],
    },
  ] as Comparison[],
};

export type PipelineStage = {
  title: string;
  detail: string;
  src: MediaPath;
  holdSeconds: number;
};

// The same shot built up pass by pass, named by an accordion bar. Stills 2-6 are the linear HDR
// buffer with exposure 1 and the sRGB encode only, stage 7 the engine's final image; Geometry is
// five debug views side by side and holds longer so they can all be read.
export const breakdownStages = {
  changeSeconds: 0.5,
  stages: [
    { title: 'Geometry', detail: 'G-buffer', src: 'stills/breakdown2/bd1_geometry_labeled.png', holdSeconds: 4 },
    { title: 'Baked Volumes', detail: 'indirect diffuse', src: 'stills/breakdown2/bd2_volumes.png', holdSeconds: 2.5 },
    { title: 'Reflection Probes', detail: 'indirect specular', src: 'stills/breakdown2/bd3_probes.png', holdSeconds: 2.5 },
    { title: 'Direct Lighting', detail: 'analytic lights + shadows', src: 'stills/breakdown2/bd4_direct.png', holdSeconds: 2.5 },
    { title: 'SSGI', detail: 'indirect diffuse', src: 'stills/breakdown2/bd5_ssgi.png', holdSeconds: 2.5 },
    { title: 'SSR', detail: 'indirect specular', src: 'stills/breakdown2/bd6_ssr.png', holdSeconds: 2.5 },
    { title: 'Post', detail: 'tonemap, bloom, fog', src: 'stills/breakdown2/bd7_post.png', holdSeconds: 2.5 },
  ] as PipelineStage[],
};

export type BreakdownLayer = {
  title: string;
  row: number;
  column: number;
  // Debug view of the technique alone, shown in its matrix cell
  debug: MediaPath;
  // The frame with this layer and every previous one enabled
  result: MediaPath;
  // What this layer added, flashed over the frame
  diff: MediaPath;
};

export const frameBreakdown = {
  phases: {
    strip: 2,
    layout: 1.5,
    perLayer: 3.5,
    collapse: 1,
    tonemap: 5.5,
  },
  final: 'stills/breakdown/final.png',
  direct: 'stills/breakdown/direct.png',
  rows: ['Diffuse', 'Specular'],
  columns: ['Baked', 'Screen space'],
  layers: [
    {
      title: 'Irradiance Volumes',
      row: 0,
      column: 0,
      debug: 'stills/breakdown/irradiance_volumes_debug.png',
      result: 'stills/breakdown/irradiance_volumes.png',
      diff: 'stills/breakdown/irradiance_volumes_diff.png',
    },
    {
      title: 'SSGI',
      row: 0,
      column: 1,
      debug: 'stills/breakdown/ssgi_debug.png',
      result: 'stills/breakdown/ssgi.png',
      diff: 'stills/breakdown/ssgi_diff.png',
    },
    {
      title: 'Reflection Probes',
      row: 1,
      column: 0,
      debug: 'stills/breakdown/reflection_probes_debug.png',
      result: 'stills/breakdown/reflection_probes.png',
      diff: 'stills/breakdown/reflection_probes_diff.png',
    },
    {
      title: 'SSR',
      row: 1,
      column: 1,
      debug: 'stills/breakdown/ssr_debug.png',
      result: 'stills/breakdown/ssr.png',
      diff: 'stills/breakdown/ssr_diff.png',
    },
  ] as BreakdownLayer[],
  tonemap: {
    title: 'Tonemapping + Post',
    detail: `exposure${DOT}bloom${DOT}grading`,
    linear: 'stills/breakdown/linear_hdr.png',
  },
};

export const frameBreakdownSeconds = (() => {
  const p = frameBreakdown.phases;
  return p.strip + p.layout + p.perLayer * frameBreakdown.layers.length + p.collapse + p.tonemap;
})();

// OBS take of the whole screen with the editor window in its top-left corner, shown 1:1 so the
// editor text stays sharp. The run starts 121.3s into the take; it is used from 122s (an empty
// editor, after the setup clicks, 4s before the first batch lands) to frame 9389, the last frame
// of the orbit shot: nothing after the sequence ends.
export const editorTour = {
  footage: 'footage/editor_tour.mp4',
  trimBeforeSeconds: 122,
  footageFrames: 9389 - 7320 + 1,
  crop: { sourceWidth: 1920, sourceHeight: 1080, x: 0, y: 0, width: 1247, height: 920 },
};

export type Attribution = {
  what: string;
  credit: string;
  license: string;
};

export const credits = {
  seconds: 6,
  name: 'Andrei Vasilev',
  links: ['github.com/andrewchik0', 'linkedin.com/in/andrei-vasilev0'],
  // Every third-party asset visible in the video must be listed with its license
  attributions: [
    { what: 'Bistro scene', credit: 'Amazon Lumberyard, NVIDIA ORCA', license: 'CC BY 4.0' },
    { what: 'Car model', credit: 'Ruf RWB "MAKKO" [33] by TRINIKZ, Sketchfab', license: 'CC BY-NC 4.0' },
    { what: 'HDRI', credit: 'Belfast Sunset (Pure Sky), Poly Haven', license: 'CC0' },
    { what: 'Music', credit: TODO, license: TODO },
  ] as Attribution[],
};

export const pendingTodos = () => {
  const todos: string[] = [];
  for (const link of credits.links) {
    if (link.includes(TODO)) todos.push(`credits link: ${link}`);
  }
  for (const a of credits.attributions) {
    if (a.credit.includes(TODO) || a.license.includes(TODO) || a.license.includes('verify')) {
      todos.push(`credits attribution: ${a.what}`);
    }
  }
  return todos;
};
