import React from 'react';
import {translate} from '@docusaurus/Translate';
import Pixel from 'pixelarticons/svg/external-link.svg';

// Pixel icon (Pixelarticons, MIT) standing in for the Docusaurus default.
export default function IconExternalLink({width = 13.5, height = 13.5}: {width?: number; height?: number}): React.ReactElement {
  return (
    <Pixel
      shapeRendering="crispEdges"
      width={width}
      height={height}
      className="pixel-icon-external"
      aria-label={translate({
        id: 'theme.IconExternalLink.ariaLabel',
        message: '(opens in new tab)',
        description: 'The ARIA label for the external link icon',
      })}
    />
  );
}
