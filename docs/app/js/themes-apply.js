import { THEMES } from "./themes.js";
import { prefs } from "./store.js";

const lum = hex => {
  const n = parseInt(hex.slice(1), 16), c = [(n >> 16) & 255, (n >> 8) & 255, n & 255].map(v => { v /= 255; return v <= .03928 ? v / 12.92 : Math.pow((v + .055) / 1.055, 2.4); });
  return .2126 * c[0] + .7152 * c[1] + .0722 * c[2];
};

export function resolvedMode(mode) {
  return mode === "system" ? (matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark") : mode;
}

export function applyTheme(id = prefs.get("theme", "classic"), mode = prefs.get("mode", "dark")) {
  const t = THEMES.find(x => x.id === id) || THEMES[0];
  const m = resolvedMode(mode);
  const p = t[m];
  const root = document.documentElement;
  for (const [k, v] of Object.entries(p)) root.style.setProperty("--" + k, v);
  root.style.setProperty("--on-accent", lum(p.accent) > .35 ? "#0a0a0a" : "#ffffff");
  root.style.setProperty("--page", m === "dark" ? p.island === "#000000" ? "#0b0c10" : p.island : p.island);
  root.dataset.mode = m;
  root.dataset.theme = t.id;
  document.querySelector('meta[name="theme-color"]')?.setAttribute("content", p.island);
}

matchMedia("(prefers-color-scheme: light)").addEventListener("change", () => { if (prefs.get("mode", "dark") === "system") applyTheme(); });
