import React from 'react';
import Pixel from 'pixelarticons/svg/home.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconHome(props: React.ComponentProps<'svg'>): React.ReactElement {
  return <Pixel shapeRendering="crispEdges"  {...props} />;
}
