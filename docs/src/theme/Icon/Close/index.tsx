import React from 'react';
import Pixel from 'pixelarticons/svg/close.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconClose(props: React.ComponentProps<'svg'>): React.ReactElement {
  return <Pixel shapeRendering="crispEdges" width={21} height={21} aria-hidden="true" {...props} />;
}
