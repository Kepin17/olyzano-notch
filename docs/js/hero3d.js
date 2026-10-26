// A small 3D scene for the install page's hero: the notch, hanging from the top edge, with files dropping into it.
// Loaded only when the device can show it (see the loader in index.html); the page works the same without it.
import * as THREE from "../vendor/three.module.min.js";
import { RoomEnvironment } from "../vendor/RoomEnvironment.js";

const TILES = [
  { name: "report.pdf", tag: "PDF", color: "#e5484d" },
  { name: "photo.jpg", tag: "JPG", color: "#30a46c", photo: true },
  { name: "assets.zip", tag: "ZIP", color: "#d39a1b" },
  { name: "notes.txt", tag: "TXT", color: "#5b8def" },
];
const LOOP = 10;                                    // seconds
const easeOutBack = x => { const c1 = 1.5, c3 = c1 + 1; return 1 + c3 * Math.pow(x - 1, 3) + c1 * Math.pow(x - 1, 2); };
const easeInOut = x => x < .5 ? 2 * x * x : 1 - Math.pow(-2 * x + 2, 2) / 2;
const clamp01 = x => Math.max(0, Math.min(1, x));

// the island as the app draws it: flared shoulders, straight sides, a rounded bottom (y goes down from the screen's edge)
function islandShape(w, h, shoulder, radius) {
  const s = new THREE.Shape(), x = w / 2;
  s.moveTo(-x - shoulder, 0);
  s.lineTo(x + shoulder, 0);
  s.quadraticCurveTo(x, 0, x, -shoulder);
  s.lineTo(x, -(h - radius));
  s.quadraticCurveTo(x, -h, x - radius, -h);
  s.lineTo(-x + radius, -h);
  s.quadraticCurveTo(-x, -h, -x, -(h - radius));
  s.lineTo(-x, -shoulder);
  s.quadraticCurveTo(-x, 0, -x - shoulder, 0);
  return s;
}
function roundedRect(w, h, r) {
  const s = new THREE.Shape(), x = w / 2, y = h / 2;
  s.moveTo(-x + r, -y); s.lineTo(x - r, -y); s.quadraticCurveTo(x, -y, x, -y + r); s.lineTo(x, y - r); s.quadraticCurveTo(x, y, x - r, y);
  s.lineTo(-x + r, y); s.quadraticCurveTo(-x, y, -x, y - r); s.lineTo(-x, -y + r); s.quadraticCurveTo(-x, -y, -x + r, -y);
  const geo = new THREE.ShapeGeometry(s, 8);
  const pos = geo.attributes.position, uv = geo.attributes.uv; // the picture covers the card: uv from the position, 0..1
  for (let i = 0; i < pos.count; i++) uv.setXY(i, (pos.getX(i) + w / 2) / w, (pos.getY(i) + h / 2) / h);
  return geo;
}

// a file tile like the shelf's: a rounded card, a document with its tag, the name
function tileTexture(tile) {
  const W = 256, H = 320, c = document.createElement("canvas");
  c.width = W; c.height = H;
  const g = c.getContext("2d");
  const rr = (x, y, w, h, r) => { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); };
  g.fillStyle = "#1c1c1e"; rr(0, 0, W, H, 44); g.fill();
  g.fillStyle = "#26262a"; rr(48, 36, 160, 160, 34); g.fill();
  if (tile.photo) {
    const gr = g.createLinearGradient(0, 40, 0, 190); gr.addColorStop(0, "#7cc4ff"); gr.addColorStop(1, "#ffcf9e");
    g.save(); rr(56, 44, 144, 144, 28); g.clip(); g.fillStyle = gr; g.fillRect(56, 44, 144, 144);
    g.fillStyle = "#3f7d4f"; g.beginPath(); g.moveTo(56, 160); g.lineTo(104, 112); g.lineTo(140, 148); g.lineTo(168, 120); g.lineTo(200, 160); g.lineTo(200, 188); g.lineTo(56, 188); g.fill();
    g.fillStyle = "#fff6d0"; g.beginPath(); g.arc(166, 78, 15, 0, 7); g.fill(); g.restore();
  } else {
    g.fillStyle = "#f3f4f6"; g.beginPath(); g.moveTo(88, 62); g.lineTo(150, 62); g.lineTo(176, 88); g.lineTo(176, 172); g.arcTo(176, 180, 168, 180, 8); g.lineTo(96, 180); g.arcTo(88, 180, 88, 172, 8); g.closePath(); g.fill();
    g.fillStyle = tile.color; rr(98, 128, 60, 28, 8); g.fill();
    g.fillStyle = "#fff"; g.font = "700 18px system-ui, sans-serif"; g.textAlign = "center"; g.fillText(tile.tag, 128, 148);
  }
  g.fillStyle = "#f5f5f7"; g.font = "600 25px system-ui, sans-serif"; g.textAlign = "center"; g.fillText(tile.name, W / 2, 246);
  g.fillStyle = "#8e8e93"; g.font = "500 20px system-ui, sans-serif"; g.fillText(tile.photo ? "2.1 MB" : tile.tag === "ZIP" ? "14 MB" : tile.tag === "TXT" ? "3 KB" : "2.4 MB", W / 2, 282);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 4;
  return t;
}

export function start(canvas) {
  const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: "low-power" });
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 1.5));
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  const scene = new THREE.Scene();
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  const camera = new THREE.PerspectiveCamera(28, 1, 0.1, 50);
  camera.position.set(0, -0.5, 11);

  const accent = new THREE.Color((getComputedStyle(document.documentElement).getPropertyValue("--accent") || "#3daee9").trim());
  const rig = new THREE.Group();                    // everything that tilts with the pointer
  scene.add(rig);

  const W = 4.6, H = 1.9, depth = 0.55;
  const island = new THREE.Mesh(
    new THREE.ExtrudeGeometry(islandShape(W, H, 0.32, 0.62), { depth, bevelEnabled: true, bevelThickness: 0.07, bevelSize: 0.07, bevelSegments: 5, curveSegments: 20 }),
    new THREE.MeshPhysicalMaterial({ color: 0x050506, metalness: 0.25, roughness: 0.28, clearcoat: 1, clearcoatRoughness: 0.12, envMapIntensity: 1.1 }));
  island.position.set(0, 2.2, -depth / 2);
  rig.add(island);

  const rim = new THREE.DirectionalLight(accent, 1.4); rim.position.set(3, 2, 3); scene.add(rim);
  const glow = new THREE.PointLight(accent, 14, 9, 1.8); glow.position.set(1.2, 0.4, 2.6); scene.add(glow);
  scene.add(new THREE.AmbientLight(0xffffff, 0.15));

  const cardGeo = roundedRect(0.92, 1.15, 0.18);
  const cards = TILES.map((tile, i) => {
    const map = tileTexture(tile);
    const mat = new THREE.MeshStandardMaterial({ map, emissive: 0xffffff, emissiveMap: map, emissiveIntensity: 0.5, roughness: 0.55, metalness: 0, transparent: true, side: THREE.DoubleSide });
    const m = new THREE.Mesh(cardGeo, mat);
    rig.add(m);
    return { m, mat, x: (i - 1.5) * 1.05, i };
  });

  function size() {
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w || !h) return;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    const fit = Math.min(1, (w / h) * 0.85);        // a narrow area shows the island a little smaller
    rig.scale.setScalar(fit);
    camera.updateProjectionMatrix();
  }
  size();
  const ro = new ResizeObserver(size); ro.observe(canvas);

  let mx = 0, my = 0, tx = 0, ty = 0;
  const onMove = e => { mx = (e.clientX / innerWidth) * 2 - 1; my = (e.clientY / innerHeight) * 2 - 1; };
  addEventListener("pointermove", onMove, { passive: true });

  let raf = 0, visible = true, running = true, t0 = performance.now();
  function frame(now) {
    raf = requestAnimationFrame(frame);
    const t = ((now - t0) / 1000);
    tx += (mx * 0.28 - tx) * 0.05; ty += (my * 0.12 - ty) * 0.05;
    rig.rotation.y = tx + Math.sin(t * 0.4) * 0.06; rig.rotation.x = ty;
    for (const c of cards) {
      const local = ((t - c.i * 0.38) % LOOP + LOOP) % LOOP;          // seconds into this card's loop
      const fall = easeOutBack(clamp01(local / 1.3));                 // 0 above the island ... 1 docked
      const out = easeInOut(clamp01((local - 7.2) / 1.6));            // 0 docked ... 1 gone to the right
      const y = 1.45 + (1 - fall) * 3.4;                              // docked row sits on the island's face
      c.m.position.set(c.x + out * 3.4, y - out * 0.3, 0.45 + (1 - fall) * 0.6 + Math.sin(t * 1.3 + c.i) * 0.02);
      c.m.rotation.set((1 - fall) * 0.7, (1 - fall) * (c.i % 2 ? -0.5 : 0.5), (1 - fall) * (c.i % 2 ? 0.35 : -0.35) + Math.sin(t * 0.9 + c.i) * 0.015);
      c.mat.opacity = clamp01(local / 0.25) * (1 - out);
    }
    glow.intensity = 12 + Math.sin(t * 1.1) * 3;
    renderer.render(scene, camera);
  }
  function setRunning(on) {
    if (on === running) return;
    running = on;
    if (on) { t0 = performance.now() - (performance.now() - t0); raf = requestAnimationFrame(frame); } else cancelAnimationFrame(raf);
  }
  const io = new IntersectionObserver(es => { visible = es[0].isIntersecting; setRunning(visible && !document.hidden); }, { threshold: 0.05 });
  io.observe(canvas);
  const onVis = () => setRunning(visible && !document.hidden);
  document.addEventListener("visibilitychange", onVis);
  raf = requestAnimationFrame(frame);
  canvas.classList.add("on");

  return {
    stop() {
      cancelAnimationFrame(raf); running = false;
      io.disconnect(); ro.disconnect(); removeEventListener("pointermove", onMove); document.removeEventListener("visibilitychange", onVis);
      cardGeo.dispose(); cards.forEach(c => { c.mat.map.dispose(); c.mat.dispose(); });
      island.geometry.dispose(); island.material.dispose(); pmrem.dispose(); renderer.dispose();
      canvas.classList.remove("on");
    },
  };
}
