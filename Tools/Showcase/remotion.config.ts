import { Config } from '@remotion/cli/config';

Config.setVideoImageFormat('jpeg');
Config.setJpegQuality(95);
Config.setCodec('h264');
// High enough that YouTube's re-encode, not ours, is the quality bottleneck
Config.setCrf(16);
Config.setOverwriteOutput(true);
