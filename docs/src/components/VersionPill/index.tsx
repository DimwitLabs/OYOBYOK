import useDocusaurusContext from "@docusaurus/useDocusaurusContext";

export default function VersionPill() {
  const { siteConfig } = useDocusaurusContext();
  const version = siteConfig.customFields?.appVersion as string;
  const repo = siteConfig.customFields?.repo as string;
  if (!version) return null;
  return (
    <a className="version-pill" href={`${repo}/releases/tag/v${version}`} target="_blank" rel="noopener noreferrer" title={`OYOBYOK ${version}`}>
      v{version}
    </a>
  );
}
