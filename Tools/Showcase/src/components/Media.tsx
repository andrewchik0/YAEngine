import { getStaticFiles } from '@remotion/studio';
import React from 'react';
import { AbsoluteFill, Img, OffthreadVideo, Series, staticFile } from 'remotion';
import { sec } from '../design/tokens';
import type { Clip, MediaPath } from '../shots/timeline';
import { Slate } from './Slate';

const VIDEO_EXTENSIONS = ['.mp4', '.mov', '.mkv', '.webm'];

const isVideo = (path: MediaPath) => VIDEO_EXTENSIONS.some((ext) => path.toLowerCase().endsWith(ext));

// Static file names use the OS separator on Windows
export const hasStaticFile = (path: MediaPath) =>
  getStaticFiles().some((file) => file.name.replaceAll('\\', '/') === path);

type MediaProps = {
  src: MediaPath;
  label?: string;
  trimBeforeSeconds?: number;
  placeholderBrightness?: number;
  placeholderAlign?: 'center' | 'left' | 'right';
  style?: React.CSSProperties;
};

// A video or a still from public/, or a labeled placeholder while the file does not exist yet
export const Media: React.FC<MediaProps> = ({
  src,
  label,
  trimBeforeSeconds,
  placeholderBrightness,
  placeholderAlign,
  style,
}) => {
  if (!hasStaticFile(src)) {
    return (
      <Slate
        title={label ?? src}
        detail={`public/${src}`}
        brightness={placeholderBrightness}
        align={placeholderAlign}
        style={style}
      />
    );
  }

  const fill: React.CSSProperties = { width: '100%', height: '100%', objectFit: 'cover' };
  return (
    <AbsoluteFill style={style}>
      {isVideo(src) ? (
        <OffthreadVideo
          src={staticFile(src)}
          muted
          trimBefore={trimBeforeSeconds ? sec(trimBeforeSeconds) : undefined}
          style={fill}
        />
      ) : (
        <Img src={staticFile(src)} style={fill} />
      )}
    </AbsoluteFill>
  );
};

// Hard cuts between clips, back to back
export const ClipSequence: React.FC<{ clips: Clip[] }> = ({ clips }) => (
  <Series>
    {clips.map((clip, index) => (
      <Series.Sequence key={index} durationInFrames={sec(clip.seconds)}>
        <Media src={clip.src} label={clip.label} trimBeforeSeconds={clip.trimBeforeSeconds} />
      </Series.Sequence>
    ))}
  </Series>
);
