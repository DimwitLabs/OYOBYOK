import React from 'react';
import Pixel from 'pixelarticons/svg/chevron-right.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconArrow(props: React.ComponentProps<'svg'>): React.ReactElement {
  return <Pixel shapeRendering="crispEdges" width={20} height={20} aria-hidden="true" {...props} />;
}
