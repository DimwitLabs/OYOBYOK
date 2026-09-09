import React, { useEffect, useState } from "react";
import Layout from "@theme/Layout";
import Link from "@docusaurus/Link";
import useBaseUrl from "@docusaurus/useBaseUrl";
import BrowserOnly from "@docusaurus/BrowserOnly";
import styles from "./index.module.css";

const DIFFERENCES = [
  { k: "git", t: "Git over SSH", d: "Each session is a commit on a remote you run, signed with your own key." },
  { k: "sftp", t: "SFTP", d: "Or push the projects to any SSH server you already have." },
  { k: "wifi", t: "WiFi only while syncing", d: "The radio stays off while you write and comes up for the Synchronise page." },
  { k: "folders", t: "Folder navigation", d: "Files and folders you move through, instead of one project held open the whole time." },
  { k: "manage", t: "Manage mode", d: "Rename, move and delete files and folders on the device itself." },
  { k: "keyboard", t: "Keyboard support", d: "Only wireless keyboards are tested so far; USB keyboards are not supported yet." },
  { k: "led", t: "Status LED", d: "Diverse states for charging, saved, syncing, keyboard connected, and more." },
  { k: "open", t: "Open source", d: "C on ESP-IDF, MIT licensed, written from scratch." },
];

const SPLASHES = [
  { f: "splash/0.png", q: "Everything was beautiful and nothing hurt.", by: "Kurt Vonnegut" },
  { f: "splash/1.png", q: "I would always rather be happy than dignified.", by: "Charlotte Bronte" },
  { f: "splash/2.png", q: "It was the best of times, it was the worst of times.", by: "Charles Dickens" },
  { f: "splash/3.png", q: "There is nothing to writing.", by: "Ernest Hemingway" },
  { f: "splash/4.png", q: "The first draft of anything is shit.", by: "Ernest Hemingway" },
];

function Splash({ base }: { base: string }): React.ReactElement {
  // Two layers: the one underneath is what was showing, the one on top fades in over it.
  const [layers, setLayers] = useState<[number, number | null]>([0, null]);
  useEffect(() => {
    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    let cur = Math.floor(Math.random() * SPLASHES.length);
    setLayers([cur, null]);
    if (reduced) return;
    SPLASHES.forEach((s) => { const img = new Image(); img.src = base + s.f; });
    const t = window.setInterval(() => {
      let next = Math.floor(Math.random() * (SPLASHES.length - 1));
      if (next >= cur) next += 1;
      setLayers([cur, next]);
      cur = next;
    }, 4200);
    return () => clearInterval(t);
  }, [base]);
  const [under, over] = layers;
  const u = SPLASHES[under];
  const o = over === null ? null : SPLASHES[over];
  return (
    <div className={styles.splashStack}>
      <img className={styles.splash} src={base + u.f} alt={`The boot splash: ${u.q} ${u.by}.`} />
      {o && (
        <img
          key={o.f}
          className={styles.splashOver}
          src={base + o.f}
          alt=""
          onAnimationEnd={() => setLayers([over as number, null])}
        />
      )}
    </div>
  );
}

const SHOTS = [
  { f: "git-syncing.png", c: "Sync: commit, fetch, rebase, push" },
  { f: "sftp-pushing.png", c: "SFTP push to your own server" },
  { f: "manage.png", c: "Manage mode on the device" },
  { f: "synchronise.png", c: "The Synchronise page, where WiFi lives" },
];

export default function Home(): React.ReactElement {
  const shots = useBaseUrl("/img/screens/");
  return (
    <Layout title="Own Your Own Bring Your Own Keyboard" description="OYOBYOK is an alternative firmware for the BYOK writing device: your writing in Git, on a remote you control, with no account in between.">
      <main className={styles.page}>
        <section className={styles.hero}>
          <div className={styles.copy}>
            <p className={styles.eyebrow}>Alternative firmware for the BYOK writing device</p>
            <h1 className={styles.title}>OYOBYOK</h1>
            <p className={styles.tagline}>Own Your Own Bring Your Own Keyboard.</p>
            <p className={styles.lede}>
              The same lovely little device, with your own plumbing underneath. Your drafts go to a Git remote you control, signed with a key you made, one commit per session. No account in between you and your files.
            </p>
            <div className={styles.actions}>
              <Link className={styles.primary} to="/docs">Read the docs</Link>
              <Link className={styles.secondary} to="/docs/getting-started/flashing">Flash it</Link>
              <Link className={styles.secondary} href="https://github.com/DimwitLabs/OYOBYOK">GitHub</Link>
            </div>
          </div>
          <figure className={styles.stage}>
            <BrowserOnly fallback={<Splash base={shots} />}>
              {() => {
                const Device3D = require("@site/src/components/Device3D").default;
                return <Device3D />;
              }}
            </BrowserOnly>
          </figure>
        </section>

        <section className={styles.section}>
          <h2 className={styles.h2}>Before You Flash</h2>
          <div className={styles.aside}>
            <p>
              <strong>Use it if</strong> you already keep your writing in Git or on a server you run, and you want the device to speak to that and nothing else. It is a firmware for people who like owning the plumbing.
            </p>
            <p>
              <strong>Skip it if</strong> the Studio app and its sync are part of why you bought the device. The stock firmware does those well, and OYOBYOK does not do them at all.
            </p>
            <p>
              <strong>Your call.</strong> Flashing replaces the stock firmware and may cost you the warranty and support that came with the device. Read what you agreed to when you bought it, and decide for yourself.
            </p>
          </div>
        </section>

        <section className={styles.section}>
          <h2 className={styles.h2}>What It Does Differently</h2>
          <dl className={styles.ledger}>
            {DIFFERENCES.map((f) => (
              <div key={f.k} className={styles.row}>
                <dt>{f.t}</dt>
                <dd>{f.d}</dd>
              </div>
            ))}
          </dl>
        </section>

        <section className={styles.section}>
          <h2 className={styles.h2}>On the Screen</h2>
          <ul className={styles.strip}>
            {SHOTS.map((s) => (
              <li key={s.f}>
                <img src={shots + s.f} alt={s.c} loading="lazy" />
                <figcaption>{s.c}</figcaption>
              </li>
            ))}
          </ul>
        </section>

        <section className={styles.note}>
          <p>
            Released under the MIT License. Backed by the <a href="https://dimwit.me/pledge">Dimwit Pledge</a>.
          </p>
          <p>
            BYOK is a trademark of <a href="https://byok.io/">BYOK</a>. OYOBYOK is an independent project with no affiliation to them, written with a lot of affection for their hardware. Flashing replaces the stock firmware and is your own decision. It has been tested on exactly one device. The device on this page is drawn from the case model BYOK publishes.
          </p>
        </section>
      </main>
    </Layout>
  );
}
