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

// A rectangle of the source frame, in source pixels, shown instead of the whole frame. The box
// the media fills must have the crop's aspect.
export type MediaCrop = {
  sourceWidth: number;
  sourceHeight: number;
  x: number;
  y: number;
  width: number;
  height: number;
};

type MediaProps = {
  src: MediaPath;
  label?: string;
  trimBeforeSeconds?: number;
  crop?: MediaCrop;
  placeholderBrightness?: number;
  placeholderAlign?: 'center' | 'left' | 'right';
  style?: React.CSSProperties;
};

const cropStyle = (crop: MediaCrop): React.CSSProperties => ({
  position: 'absolute',
  left: `${(-crop.x / crop.width) * 100}%`,
  top: `${(-crop.y / crop.height) * 100}%`,
  width: `${(crop.sourceWidth / crop.width) * 100}%`,
  height: `${(crop.sourceHeight / crop.height) * 100}%`,
  maxWidth: 'none',
});

// A video or a still from public/, or a labeled placeholder while the file does not exist yet
export const Media: React.FC<MediaProps> = ({
  src,
  label,
  trimBeforeSeconds,
  crop,
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

  const fill: React.CSSProperties = crop ? cropStyle(crop) : { width: '100%', height: '100%', objectFit: 'cover' };
  return (
    <AbsoluteFill style={{ overflow: 'hidden', ...style }}>
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
