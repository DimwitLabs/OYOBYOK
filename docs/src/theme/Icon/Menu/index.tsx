import React from 'react';
import Pixel from 'pixelarticons/svg/menu.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconMenu(props: React.ComponentProps<'svg'>): React.ReactElement {
  return <Pixel shapeRendering="crispEdges" width={30} height={30} aria-hidden="true" {...props} />;
}
