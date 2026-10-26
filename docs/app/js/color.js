// Colour picker: the EyeDropper API where the browser has it, else click a pixel of a picture.
import { h, $, mount, toast } from "./util.js";
import { icon } from "./icons.js";
import { prefs } from "./store.js";

const rgbOf = hex => { const n = parseInt(hex.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };
export function fmt(hex, kind) {
  const [r, g, b] = rgbOf(hex);
  if (kind === "rgb") return `rgb(${r}, ${g}, ${b})`;
  if (kind === "hsl") {
    const R = r / 255, G = g / 255, B = b / 255, mx = Math.max(R, G, B), mn = Math.min(R, G, B), l = (mx + mn) / 2, d = mx - mn;
    let hh = 0, s = 0;
    if (d) { s = d / (1 - Math.abs(2 * l - 1)); hh = mx === R ? ((G - B) / d) % 6 : mx === G ? (B - R) / d + 2 : (R - G) / d + 4; hh = Math.round(((hh * 60) + 360) % 360); }
    return `hsl(${hh}, ${Math.round(s * 100)}%, ${Math.round(l * 100)}%)`;
  }
  return hex.toUpperCase();
}
const readable = hex => { const [r, g, b] = rgbOf(hex).map(v => { v /= 255; return v <= .03928 ? v / 12.92 : Math.pow((v + .055) / 1.055, 2.4); }); return .2126 * r + .7152 * g + .0722 * b > .179 ? "#000" : "#fff"; };

let kind = "hex", copied = "";
const colors = () => prefs.get("colors", []);
const add = hex => { prefs.set("colors", [hex.toUpperCase(), ...colors().filter(c => c !== hex.toUpperCase())].slice(0, 30)); };
const copy = async (hex, ctx) => { await navigator.clipboard.writeText(fmt(hex, kind)); copied = hex; toast("Copied " + fmt(hex, kind)); ctx.refresh(); setTimeout(() => { copied = ""; }, 1400); };

export async function render(ctx) {
  const list = colors(), cur = list[0];
  const picker = h("input", { type: "file", accept: "image/*", hidden: true, onchange: e => fromImage(e.target.files[0], ctx) });
  mount(ctx.page,
    h("div", { class: "row" },
      "EyeDropper" in window ? h("button", { class: "tbtn primary", onclick: async () => { try { const r = await new EyeDropper().open(); add(r.sRGBHex); ctx.refresh(); } catch { /* cancelled */ } } }, h("span", { html: icon("palette", 14) }), "Pick from the screen") : null,
      h("button", { class: "tbtn", onclick: () => picker.click() }, h("span", { html: icon("image", 14) }), "Pick from a picture"), picker,
      h("span", { class: "grow" }),
      list.length ? h("div", { class: "seg" }, ...[["hex", "HEX"], ["rgb", "RGB"], ["hsl", "HSL"]].map(([v, t]) => h("button", { "aria-pressed": String(kind === v), onclick: () => { kind = v; ctx.refresh(); } }, t))) : null),
    !("EyeDropper" in window) ? h("p", { class: "muted" }, "Your browser cannot pick from the screen; choose a picture and click a pixel of it.") : null,
    h("div", { id: "imgPick" }),
    cur ? h("div", { class: "hero-swatch", style: { background: cur, color: readable(cur), margin: "12px 0" }, onclick: () => copy(cur, ctx), title: "Click to copy" },
      h("b", {}, fmt(cur, kind)), h("span", { style: { opacity: .7, fontFamily: "var(--mono)", fontSize: "12px" } }, kind === "hex" ? fmt(cur, "rgb") : fmt(cur, "hex")), h("span", { style: { fontSize: "12px", fontWeight: 600 } }, copied === cur ? "✓ Copied" : "Click to copy")) :
      h("p", { class: "muted", style: { textAlign: "center", margin: "22px 0" } }, "No colors yet. The ones you pick are kept here: click one to copy it."),
    list.length > 1 ? h("div", { class: "row" }, ...list.slice(1).map(c => h("button", { class: "sw", style: { background: c }, title: fmt(c, kind) + " · click to copy · right-click to remove", "aria-label": c,
      onclick: () => copy(c, ctx), oncontextmenu: e => { e.preventDefault(); prefs.set("colors", colors().filter(x => x !== c)); ctx.refresh(); } }))) : null);
}

async function fromImage(file, ctx) {
  if (!file) return;
  const bmp = await createImageBitmap(file), max = 560, k = Math.min(1, max / bmp.width);
  const c = h("canvas", { width: Math.round(bmp.width * k), height: Math.round(bmp.height * k), style: { width: "100%", maxWidth: max + "px", borderRadius: "14px", cursor: "crosshair", margin: "10px 0" } });
  c.getContext("2d").drawImage(bmp, 0, 0, c.width, c.height);
  c.addEventListener("click", e => {
    const r = c.getBoundingClientRect(), x = Math.floor((e.clientX - r.left) * c.width / r.width), y = Math.floor((e.clientY - r.top) * c.height / r.height);
    const [R, G, B] = c.getContext("2d").getImageData(x, y, 1, 1).data;
    add("#" + [R, G, B].map(v => v.toString(16).padStart(2, "0")).join(""));
    ctx.refresh();
  });
  $("#imgPick").replaceChildren(h("p", { class: "muted" }, "Click a pixel of the picture."), c);
}
