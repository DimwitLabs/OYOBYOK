import type { SidebarsConfig } from "@docusaurus/plugin-content-docs";

const sidebars: SidebarsConfig = {
  docs: [
    "intro",
    {
      type: "category",
      label: "Getting Started",
      collapsed: false,
      items: ["getting-started/flashing", "getting-started/first-boot"],
    },
    {
      type: "category",
      label: "Using OYOBYOK",
      collapsed: false,
      items: ["using/splash", "using/writing", "using/git-sync", "using/sftp", "using/disk-mode", "using/settings", "using/led"],
    },
    {
      type: "category",
      label: "Reference",
      collapsed: false,
      items: [
        "reference/configuration",
        "reference/hardware",
        "reference/architecture",
        { type: "link", label: "Changelog", href: "https://github.com/DeepanshKhurana/OYOBYOK/blob/main/CHANGELOG.md" },
      ],
    },
  ],
};

export default sidebars;
