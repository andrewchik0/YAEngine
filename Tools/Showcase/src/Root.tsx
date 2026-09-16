import React from 'react';
import { Composition, Folder } from 'remotion';
import { VIDEO } from './design/tokens';
import { SEGMENTS, Showcase, SHOWCASE_FRAMES } from './Showcase';
import { pendingTodos } from './shots/timeline';

const todos = pendingTodos();
if (todos.length > 0) {
  console.warn(`Showcase has unfinished data:\n  ${todos.join('\n  ')}`);
}

export const RemotionRoot: React.FC = () => (
  <>
    <Composition id="Showcase" component={Showcase} durationInFrames={SHOWCASE_FRAMES} {...VIDEO} />
    <Folder name="Segments">
      {SEGMENTS.map(({ id, frames, Component }) => (
        <Composition key={id} id={id} component={Component} durationInFrames={frames} {...VIDEO} />
      ))}
    </Folder>
  </>
);
