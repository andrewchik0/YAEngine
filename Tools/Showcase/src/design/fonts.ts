import { loadFont as loadInter } from '@remotion/google-fonts/Inter';
import { loadFont as loadJetBrainsMono } from '@remotion/google-fonts/JetBrainsMono';

const inter = loadInter('normal', { weights: ['400', '500', '700'], subsets: ['latin'] });
const mono = loadJetBrainsMono('normal', { weights: ['400', '700'], subsets: ['latin'] });

export const fonts = {
  sans: inter.fontFamily,
  mono: mono.fontFamily,
} as const;
