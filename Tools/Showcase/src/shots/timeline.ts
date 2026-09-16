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

export const MODES = {
  raster: { title: 'Rasterizer', detail: `TAA${DOT}1080p` },
  pathTracer: { title: 'Path tracer', detail: `DLSS Balanced${DOT}1080p` },
} satisfies Record<string, Mode>;

export const intro = {
  seconds: 4,
  title: 'YAEngine',
  author: 'made by Andrei Vasilev',
  capture: `Captured in real time on an RTX 4060 Ti 8GB${DOT}1080p`,
};

export const flythrough = {
  mode: MODES.raster,
  clips: [{ src: 'footage/flythrough.mp4', seconds: 20, label: 'Release flythrough + PresentMon' }] as Clip[],
};

export const rasterVsPathTracing = {
  comparisons: [
    {
      name: 'Exterior',
      seconds: 5.5,
      raster: 'footage/exterior_raster.mp4',
      pathTraced: 'footage/exterior_pt.mp4',
      callout: {
        atSeconds: 2.2,
        seconds: 3,
        x: 1180,
        y: 640,
        title: 'Clear coat',
        detail: 'two-layer material',
      } as CalloutSpec | undefined,
    },
    {
      name: 'Interior',
      seconds: 5.5,
      raster: 'footage/interior_raster.mp4',
      pathTraced: 'footage/interior_pt.mp4',
      callout: undefined as CalloutSpec | undefined,
    },
  ],
  glass: {
    seconds: 4,
    still: 'stills/glass_pt.png',
    title: 'Refractive glass',
    aside: 'not real-time though :(',
  },
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
  links: [`github.com/${TODO}`, `linkedin.com/in/${TODO}`],
  tech: `C++23${DOT}Vulkan${DOT}GLSL${DOT}DLSS Ray Reconstruction`,
  // Every third-party asset visible in the video must be listed with its license
  attributions: [
    { what: 'Bistro scene', credit: 'Amazon Lumberyard, NVIDIA ORCA', license: 'CC BY 4.0 (verify)' },
    { what: 'Car model', credit: TODO, license: TODO },
    { what: 'HDRI', credit: TODO, license: TODO },
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
