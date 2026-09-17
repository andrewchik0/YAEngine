import { Config } from '@remotion/cli/config';

Config.setVideoImageFormat('jpeg');
Config.setJpegQuality(95);
Config.setCodec('h264');
// High enough that YouTube's re-encode, not ours, is the quality bottleneck
Config.setCrf(16);
// The recordings are bt709; without this the output is tagged bt470bg and full range, and a
// player that honours the tags shows colors that do not match the footage
Config.setColorSpace('bt709');
Config.setOverwriteOutput(true);
