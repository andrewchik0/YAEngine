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

export type CalloutSpec = {
  atSeconds: number;
  seconds: number;
  // Target point in output pixels
  x: number;
  y: number;
  title: string;
  detail: string;
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
    detail: 'render pipeline, layer by layer',
  },
  editor: {
    seconds: 3.5,
    title: 'It has an editor',
    detail: 'The tour is driven by an AI agent over MCP',
  },
} satisfies Record<string, TitleCardSpec>;

export const flythrough = {
  mode: MODES.raster,
  // The recording opens with a false start and a three second pause; the good take runs
  // 11.6s to 35.6s, and the camera jumps to the next sequencer shot at 35.5s - stop short
  // of it or the cut into the next card catches the teleport
  clips: [{ src: 'footage/intro_new.mp4', seconds: 23.2, trimBeforeSeconds: 11.6 }] as Clip[],
};

// One rendering mode of one locked-off shot
export type Stage = {
  mode: Mode;
  src: MediaPath;
  // Into the recording. Every clip is a static camera, so the window is picked by how steady
  // and how high the burned-in fps reads. A still has none.
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
        { mode: MODES.raster, src: 'footage/raster_0.mp4', trimBeforeSeconds: 4.5 },
        { mode: MODES.pathTracer, src: 'footage/pt_0.mp4', trimBeforeSeconds: 1 },
      ],
      callout: {
        atSeconds: 4.2,
        seconds: 3,
        x: 1180,
        y: 640,
        title: 'Clear coat',
        detail: 'two-layer material',
      },
    },
    {
      name: 'Exterior, front',
      stages: [
        { mode: MODES.raster, src: 'footage/raster_1.mp4', trimBeforeSeconds: 14.5 },
        { mode: MODES.pathTracer, src: 'footage/pt_1.mp4', trimBeforeSeconds: 10 },
      ],
    },
    {
      name: 'Interior',
      stages: [
        { mode: MODES.raster, src: 'footage/raster_2.mp4', trimBeforeSeconds: 14.5 },
        { mode: MODES.pathTracer, src: 'footage/pt_2.mp4', trimBeforeSeconds: 3 },
        { mode: MODES.pathTracerGlass, src: 'footage/pt_2_real_glass.mp4', trimBeforeSeconds: 2 },
      ],
    },
  ] as Comparison[],
};

// One shot lit up layer by layer, swept with the same line the mode comparison uses.
// Stages 1-5 are the renderer's linear HDR buffer with exposure and the sRGB encode only,
// no tone map, so the grade arriving with Post is part of what the last step visibly adds;
// stage 6 is the engine's own final image. See docs/frame-capture.md for the capture recipe.
export const breakdownSweep = {
  holdSeconds: 2,
  sweepSeconds: 2.6,
  stages: [
    { mode: { title: 'Baked Volumes', detail: 'indirect diffuse' }, src: 'stills/breakdown/bd1_volumes.png' },
    { mode: { title: 'SSGI', detail: 'indirect diffuse' }, src: 'stills/breakdown/bd2_ssgi.png' },
    { mode: { title: 'Reflection Probes', detail: 'indirect specular' }, src: 'stills/breakdown/bd3_probes.png' },
    { mode: { title: 'SSR', detail: 'indirect specular' }, src: 'stills/breakdown/bd4_ssr.png' },
    { mode: { title: 'Direct Light', detail: 'analytic lights + shadows' }, src: 'stills/breakdown/bd5_direct.png' },
    { mode: { title: 'Post', detail: 'tonemap, bloom, fog' }, src: 'stills/breakdown/bd6_post.png' },
  ] as Stage[],
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

export const editorTour = {
  seconds: 21,
  footage: 'footage/editor_tour.mp4',
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
