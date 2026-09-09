import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import type * as Preset from "@docusaurus/preset-classic";
import type { Config } from "@docusaurus/types";
import { themes as prismThemes } from "prism-react-renderer";

const ORG = "DimwitLabs";
const DOMAIN = "oyobyok.dimwit.me";
const NAME = "OYOBYOK";
const REPO = `https://github.com/${ORG}/${NAME}`;

// The firmware version lives in one place, shared/oyobyok_app.inc; the navbar pill reads it from there.
function firmwareVersion() {
  try {
    const here = fileURLToPath(new URL(".", import.meta.url));
    const src = readFileSync(`${here}../firmware/shared/oyobyok_app.inc`, "utf8");
    return src.match(/#define OYOBYOK_VERSION "([^"]+)"/)?.[1] ?? "";
  } catch {
    return "";
  }
}

const config: Config = {
  title: "OYOBYOK",
  tagline: "Own Your Own Bring Your Own Keyboard",
  favicon: "img/oyobyok.svg",

  // GitHub Pages behind a custom domain, so the site lives at the root. static/CNAME keeps the domain across deploys.
  url: `https://${DOMAIN}`,
  baseUrl: "/",
  trailingSlash: false,

  organizationName: ORG,
  projectName: NAME,

  onBrokenLinks: "throw",
  onBrokenAnchors: "throw",

  markdown: {
    format: "mdx",
    hooks: { onBrokenMarkdownLinks: "warn" },
  },

  customFields: {
    appVersion: firmwareVersion(),
    repo: REPO,
  },

  future: { v4: true },

  i18n: { defaultLocale: "en", locales: ["en"] },

  presets: [
    [
      "classic",
      {
        docs: {
          routeBasePath: "/docs",
          sidebarPath: "./sidebars.ts",
          editUrl: `${REPO}/tree/main/docs/`,
        },
        blog: false,
        pages: {},
        theme: { customCss: ["./src/css/custom.css"] },
        sitemap: { lastmod: "date", changefreq: "weekly" },
      } satisfies Preset.Options,
    ],
  ],

  themes: [
    [
      "@easyops-cn/docusaurus-search-local",
      {
        docsRouteBasePath: "/",
        indexBlog: false,
        hashed: true,
        highlightSearchTermsOnTargetPage: true,
        searchResultLimits: 8,
        searchResultContextMaxLength: 60,
      },
    ],
  ],

  themeConfig: {
    image: "img/og.png",
    metadata: [
      { name: "description", content: "Documentation for OYOBYOK, an alternative firmware for the BYOK writing device." },
    ],
    colorMode: { defaultMode: "light", disableSwitch: false, respectPrefersColorScheme: true },
    navbar: {
      title: "OYOBYOK",
      logo: { alt: "OYOBYOK", src: "img/oyobyok.svg", href: "/", height: 28 },
      items: [
        { to: "/docs", label: "Docs", position: "left", activeBaseRegex: "^/docs/?$" },
        { to: "/docs/getting-started/flashing", label: "Flashing", position: "left" },
        { to: "/docs/reference/hardware", label: "Reference", position: "left" },
        { type: "search", position: "right" },
        { type: "custom-versionPill", position: "right" },
      ],
    },
    footer: {
      style: "light",
      links: [
        {
          title: "Docs",
          items: [
            { label: "Overview", to: "/docs" },
            { label: "Flashing", to: "/docs/getting-started/flashing" },
            { label: "First boot", to: "/docs/getting-started/first-boot" },
          ],
        },
        {
          title: "Reference",
          items: [
            { label: "Configuration", to: "/docs/reference/configuration" },
            { label: "Hardware", to: "/docs/reference/hardware" },
            { label: "Architecture", to: "/docs/reference/architecture" },
          ],
        },
        {
          title: "Project",
          items: [
            { label: "GitHub", href: REPO },
            { label: "Changelog", href: `${REPO}/blob/main/CHANGELOG.md` },
            { label: "License", href: `${REPO}/blob/main/LICENSE` },
          ],
        },
      ],
      copyright:
      'OYOBYOK is released under the MIT License. It is an independent project with no affiliation to BYOK; BYOK is a trademark of <a href="https://byok.io/" target="_blank" rel="noopener noreferrer">BYOK</a>.',
    },
    docs: { sidebar: { hideable: false, autoCollapseCategories: false } },
    tableOfContents: { minHeadingLevel: 2, maxHeadingLevel: 3 },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ["bash", "ini"],
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
