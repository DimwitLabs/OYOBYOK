import React, { useEffect, useRef } from "react";
import * as THREE from "three";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import useBaseUrl from "@docusaurus/useBaseUrl";

const LED_COLOURS = [0x3fb950, 0xe0a160, 0x4aa8ff, 0xffffff, 0xe0533f, 0xb26aff];   // connected, charging, syncing, saved, low, pairing
const SPLASHES = ["splash/0.png", "splash/1.png", "splash/2.png", "splash/3.png", "splash/4.png"];
const WIN = { x0: -25.5, x1: 19.5, y0: -71.5, y1: 65.5, z: 5.55 };

type Props = { onReady?: () => void };

export default function Device3D({ onReady }: Props): React.ReactElement {
  const host = useRef<HTMLDivElement>(null);
  const screensBase = useBaseUrl("/img/screens/");
  const modelUrl = useBaseUrl("/models/byok.glb");

  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    let disposed = false;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    el.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(26, 1, 10, 2000);
    camera.position.set(0, 26, 420);
    camera.lookAt(0, 0, 0);

    scene.add(new THREE.HemisphereLight(0xfff3e0, 0x2a2119, 1.4));
    const key = new THREE.DirectionalLight(0xffffff, 2.4); key.position.set(60, 340, 200); scene.add(key);
    const fill = new THREE.DirectionalLight(0xf7d3a8, 0.8); fill.position.set(-220, 40, 120); scene.add(fill);
    const rim = new THREE.DirectionalLight(0xf7d3a8, 1.4); rim.position.set(-120, 120, -260); scene.add(rim);

    const device = new THREE.Group();
    device.rotation.z = -Math.PI / 2;
    const rig = new THREE.Group();
    rig.add(device);
    scene.add(rig);

    const sc = document.createElement("canvas"); sc.width = 512; sc.height = 256;
    const sctx = sc.getContext("2d")!;
    const g = sctx.createRadialGradient(256, 128, 0, 256, 128, 256);
    g.addColorStop(0, "rgba(58,40,24,0.42)"); g.addColorStop(0.4, "rgba(58,40,24,0.2)"); g.addColorStop(0.8, "rgba(58,40,24,0)"); g.addColorStop(1, "rgba(58,40,24,0)");
    sctx.save(); sctx.scale(1, 0.5); sctx.fillStyle = g; sctx.fillRect(0, 0, 512, 512); sctx.restore();
    const stex = new THREE.CanvasTexture(sc); stex.colorSpace = THREE.SRGBColorSpace;
    const shadow = new THREE.Mesh(new THREE.PlaneGeometry(210, 100), new THREE.MeshBasicMaterial({ map: stex, transparent: true, depthWrite: false }));
    shadow.rotation.x = -Math.PI / 2; shadow.position.set(0, -42, 4);
    rig.add(shadow);

    const materials: Record<string, THREE.Material> = {
      FRONT_BYOK_LOGO: new THREE.MeshStandardMaterial({ color: 0x1b1a18, roughness: 0.88, metalness: 0.04 }),
      LCD: new THREE.MeshStandardMaterial({ color: 0x1e1d1b, roughness: 0.55, metalness: 0.1 }),
      CONTROL_BUTTONS: new THREE.MeshStandardMaterial({ color: 0x151412, roughness: 0.8 }),
      POWER_BUTTON: new THREE.MeshStandardMaterial({ color: 0x151412, roughness: 0.8 }),
      BIVAR: new THREE.MeshStandardMaterial({ color: 0x0c0c0c, emissive: 0x3fb950, emissiveIntensity: 1.6, roughness: 0.4 }),
      PRODUCT_LABEL: new THREE.MeshStandardMaterial({ color: 0xd9c8ac, roughness: 0.9 }),
      INLAY_BACK: new THREE.MeshStandardMaterial({ color: 0x242220, roughness: 0.97 }),
    };
    let led: THREE.MeshStandardMaterial | null = null;
    new GLTFLoader().load(modelUrl, (gltf) => {
      if (disposed) return;
      gltf.scene.traverse((o) => {
        const mesh = o as THREE.Mesh;
        if (!mesh.isMesh) return;
        const key = Object.keys(materials).find((k) => mesh.name.startsWith(k));
        if (key) mesh.material = materials[key];
        if (key === "BIVAR") led = materials.BIVAR as THREE.MeshStandardMaterial;
      });
      device.add(gltf.scene);
      onReady?.();
    });

    const canvas = document.createElement("canvas");
    canvas.width = 240; canvas.height = 80;
    const ctx = canvas.getContext("2d")!;
    ctx.fillStyle = "#f7d3a8"; ctx.fillRect(0, 0, 240, 80);
    const tex = new THREE.CanvasTexture(canvas);
    tex.magFilter = THREE.NearestFilter; tex.minFilter = THREE.LinearFilter; tex.colorSpace = THREE.SRGBColorSpace;
    const screenMat = new THREE.MeshBasicMaterial({ map: tex });
    screenMat.toneMapped = false;   // the screenshot's own amber and ink, not a lit surface
    const screen = new THREE.Mesh(new THREE.PlaneGeometry(WIN.y1 - WIN.y0, WIN.x1 - WIN.x0), screenMat);
    screen.rotation.z = Math.PI / 2;      // its width runs along the model's Y, the long edge
    screen.position.set((WIN.x0 + WIN.x1) / 2, (WIN.y0 + WIN.y1) / 2, WIN.z);
    device.add(screen);

    const images = SPLASHES.map((n) => { const i = new Image(); i.src = screensBase + n; return i; });
    let cur = Math.floor(Math.random() * SPLASHES.length);
    const show = (i: number) => {
      const img = images[i];
      if (!img.complete || img.naturalWidth === 0) { img.onload = () => show(i); return; }
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(img, 0, 0, 240, 80);
      tex.needsUpdate = true;
    };
    show(cur);
    const timer = reduced ? 0 : window.setInterval(() => {
      let next = Math.floor(Math.random() * (SPLASHES.length - 1));
      if (next >= cur) next += 1;
      cur = next; show(cur);
    }, 4200);

    rig.rotation.set(0.07, -0.62, 0);
    rig.position.y = 14;   // room beneath for the shadow
    const clock = new THREE.Clock();

    const resize = () => {
      const w = el.clientWidth, h = el.clientHeight || Math.round(w * 0.62);
      renderer.setSize(w, h, false);
      camera.aspect = w / h;
      camera.position.z = 84 / (Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)) * camera.aspect);
      camera.updateProjectionMatrix();
    };
    const ro = new ResizeObserver(resize); ro.observe(el); resize();

    let raf = 0;
    const tick = () => {
      const t = clock.getElapsedTime();
      if (led && !reduced) {
        const phase = t * 1.4, k = Math.floor(phase) % LED_COLOURS.length;
        led.emissive.setHex(LED_COLOURS[k]);
        led.emissiveIntensity = 0.6 + Math.pow(Math.sin((phase % 1) * Math.PI), 2) * 1.6;
      }
      renderer.render(scene, camera);
      raf = requestAnimationFrame(tick);
    };
    tick();

    return () => {
      disposed = true;
      cancelAnimationFrame(raf); if (timer) clearInterval(timer); ro.disconnect();
      renderer.dispose(); el.removeChild(renderer.domElement);
    };
  }, [screensBase, modelUrl, onReady]);

  return <div ref={host} className="device3d" role="img" aria-label="The BYOK running OYOBYOK, showing a splash quote." />;
}
