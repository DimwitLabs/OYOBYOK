import React from 'react';
import Pixel from 'pixelarticons/svg/sun.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconLightMode(props: React.ComponentProps<'svg'>): React.ReactElement {
  return <Pixel shapeRendering="crispEdges" width={24} height={24} {...props} />;
}
