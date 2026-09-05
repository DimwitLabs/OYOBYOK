import React from 'react';
import Pixel from 'pixelarticons/svg/moon.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconDarkMode(props: React.ComponentProps<'svg'>): React.ReactElement {
  return <Pixel shapeRendering="crispEdges" width={24} height={24} {...props} />;
}
