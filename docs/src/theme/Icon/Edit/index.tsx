import React from 'react';
import Pixel from 'pixelarticons/svg/edit.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconEdit(props: React.ComponentProps<'svg'>): React.ReactElement {
  return <Pixel shapeRendering="crispEdges" width={18} height={18} aria-hidden="true" className="pixel-icon-edit" {...props} />;
}
