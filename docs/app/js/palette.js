import { h, $, toast } from "./util.js";
import { icon } from "./icons.js";
import { TABS, state, emit } from "./app.js";
import { THEMES } from "./themes.js";
import { applyTheme } from "./themes-apply.js";
import { prefs } from "./store.js";
import * as store from "./store.js";

// Ctrl+K: the web's launcher. Open a page, run a tool, switch theme, find a file.
export async function openPalette({ go, ctx }) {
  const dlg = $("#dlg");
  const files = (await store.files.all()).filter(f => !f.removedAt);
  const items = [
    ...TABS.map(([id, name, ic]) => ({ name: "Open " + name, hint: "page", ic, run: () => go(id) })),
    { name: "Open Settings", hint: "page", ic: "gear", run: () => go("settings") },
    { name: "Add files…", hint: "action", ic: "plus", run: () => $("#picker").click() },
    ...[["image-convert", "Convert images"], ["image-resize", "Resize images"], ["image-compress", "Compress images"], ["pdf-merge", "Merge PDFs"], ["pdf-split", "Split a PDF"], ["pdf-extract", "Extract PDF pages"],
        ["images-pdf", "Images to PDF"], ["ocr", "Read text from an image (OCR)"], ["zip", "Make a zip"], ["unzip", "Open a zip"], ["qr", "QR code"]]
      .map(([id, name]) => ({ name, hint: "tool", ic: "gear", run: () => { state.toolOpen = id; go("tools"); } })),
    ...THEMES.map(t => ({ name: "Theme: " + t.name, hint: "theme", ic: "palette", run: () => { prefs.set("theme", t.id); applyTheme(); toast(t.name); } })),
    { name: "Light mode", hint: "theme", ic: "sun", run: () => { prefs.set("mode", "light"); applyTheme(); } },
    { name: "Dark mode", hint: "theme", ic: "sun", run: () => { prefs.set("mode", "dark"); applyTheme(); } },
    ...files.map(f => ({ name: f.name, hint: "file", ic: "folder", run: () => { state.query = f.name; go("files"); } })),
  ];
  const input = h("input", { placeholder: "Type a tool, a page, a theme or a file name", "aria-label": "Command palette" });
  const list = h("div", { class: "pal-list" });
  let shown = [], cur = 0;
  const draw = () => {
    const q = input.value.trim().toLowerCase();
    shown = (q ? items.filter(i => q.split(/\s+/).every(w => i.name.toLowerCase().includes(w))) : items).slice(0, 40);
    cur = Math.min(cur, Math.max(0, shown.length - 1));
    list.replaceChildren(...shown.map((i, n) => h("button", { class: "pal-item" + (n === cur ? " on" : ""), onclick: () => run(i) }, h("span", { html: icon(i.ic, 15) }), i.name, h("small", {}, i.hint))));
    if (!shown.length) list.replaceChildren(h("p", { class: "muted", style: { padding: "10px" } }, "Nothing matches."));
  };
  const run = i => { dlg.close(); i.run(); };
  input.addEventListener("input", () => { cur = 0; draw(); });
  input.addEventListener("keydown", e => {
    if (e.key === "ArrowDown") { cur = Math.min(shown.length - 1, cur + 1); draw(); e.preventDefault(); list.querySelector(".on")?.scrollIntoView({ block: "nearest" }); }
    else if (e.key === "ArrowUp") { cur = Math.max(0, cur - 1); draw(); e.preventDefault(); list.querySelector(".on")?.scrollIntoView({ block: "nearest" }); }
    else if (e.key === "Enter" && shown[cur]) run(shown[cur]);
  });
  dlg.replaceChildren(h("div", { class: "pal-in" }, input, list));
  dlg.addEventListener("click", e => { if (e.target === dlg) dlg.close(); }, { once: true });
  draw();
  dlg.showModal();
  input.focus();
}
