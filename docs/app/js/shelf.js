// The Files page: the shelf of files, categories, search, selection and the bin.
import { h, $, formatBytes, extOf, kindOf, download, toast, openType } from "./util.js";
import { icon } from "./icons.js";
import * as store from "./store.js";
import { state, on, emit } from "./app.js";
import { askPassword } from "./dialogs.js";
import * as vault from "./vault.js";

const urls = new Map(); // id -> object URL of a thumbnail
const thumbOf = async rec => {
  if (urls.has(rec.id)) return urls.get(rec.id);
  let url = null;
  const kind = kindOf(rec);
  try {
    if (rec.locked) return null;
    if (kind === "image" && !["heic", "heif"].includes(extOf(rec.name))) {
      const bmp = await createImageBitmap(rec.blob, { resizeWidth: 160, resizeQuality: "medium" }).catch(() => null);
      if (bmp) {
        const c = new OffscreenCanvas(bmp.width, bmp.height);
        c.getContext("2d").drawImage(bmp, 0, 0);
        url = URL.createObjectURL(await c.convertToBlob({ type: "image/jpeg", quality: .8 }));
      }
    }
  } catch { /* no thumbnail */ }
  urls.set(rec.id, url);
  return url;
};

export async function visibleFiles() {
  const all = await store.files.all();
  const q = state.query.trim().toLowerCase();
  let list = state.category === "__bin" ? all.filter(f => f.removedAt) : all.filter(f => !f.removedAt);
  if (state.category && state.category !== "__bin") list = list.filter(f => f.category === state.category);
  if (q) list = list.filter(f => f.name.toLowerCase().includes(q));
  const by = {
    date: (a, b) => b.added - a.added,
    name: (a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }),
    size: (a, b) => b.size - a.size,
    type: (a, b) => extOf(a.name).localeCompare(extOf(b.name)) || a.name.localeCompare(b.name),
  }[state.sort] || ((a, b) => b.added - a.added);
  return list.sort((a, b) => (b.pinned - a.pinned) || by(a, b));
}

export async function counts() {
  const all = (await store.files.all());
  return { live: all.filter(f => !f.removedAt).length, bin: all.filter(f => f.removedAt).length, all };
}

const left = rec => {
  if (rec.removedAt) { const hrs = Math.max(0, Math.ceil((rec.removedAt + store.BIN_MS - Date.now()) / 3600000)); return hrs + " h left"; }
  if (rec.pinned || !rec.expires) return "";
  const ms = rec.expires - Date.now();
  return ms < store.DAY ? "⏳ " + Math.max(1, Math.ceil(ms / 3600000)) + " h left" : "";
};

export async function openFile(rec) {
  const blob = rec.locked ? await vault.read(rec) : rec.blob;
  if (!blob) return;
  if (kindOf(rec) === "audio") { emit("play", rec); return; }
  const type = openType(rec.name);
  if (type) {
    const url = URL.createObjectURL(new Blob([blob], { type }));
    window.open(url, "_blank", "noopener,noreferrer");
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  } else {
    download(blob, rec.name);
  }
}

async function tileFor(rec, ctx) {
  const kind = kindOf(rec), sel = state.selection.has(rec.id);
  const thumb = h("div", { class: "thumb k-" + kind }, h("span", { class: "ext" }, (extOf(rec.name) || "file").slice(0, 5).toUpperCase()));
  thumbOf(rec).then(u => { if (u) thumb.replaceChildren(h("img", { src: u, alt: "", draggable: "false" })); });
  const note = rec.locked ? "🔒 locked" : left(rec);
  const tile = h("div", { class: "tile" + (sel ? " sel" : ""), tabindex: "0", role: "button", "aria-label": rec.name, draggable: "true", "data-id": rec.id },
    thumb,
    h("div", { class: "tname" }, rec.name),
    h("div", { class: "tmeta" + (note && !rec.locked ? " warn" : "") }, note || formatBytes(rec.size)),
    rec.pinned ? h("span", { class: "badge pin", html: icon("pin", 11) }) : sel ? h("span", { class: "badge", html: icon("check", 11) }) : null,
    h("div", { class: "acts" },
      h("button", { title: rec.removedAt ? "Restore" : "Open / download", "aria-label": rec.removedAt ? "Restore" : "Open", html: icon(rec.removedAt ? "undo" : "external", 12),
        onclick: async e => { e.stopPropagation(); rec.removedAt ? (await store.restoreFiles([rec.id]), ctx.refresh()) : openFile(rec); } }),
      h("button", { title: rec.removedAt ? "Delete now" : "Remove", "aria-label": rec.removedAt ? "Delete now" : "Remove", html: icon("close", 12),
        onclick: async e => { e.stopPropagation(); rec.removedAt ? (await store.deleteForever([rec.id]), ctx.refresh()) : removeWithUndo([rec.id], ctx); } })));
  const toggle = () => { state.selection.has(rec.id) ? state.selection.delete(rec.id) : state.selection.add(rec.id); ctx.refresh(); };
  tile.addEventListener("click", e => { if (e.ctrlKey || e.metaKey || e.shiftKey || state.selection.size) toggle(); else openFile(rec); });
  tile.addEventListener("keydown", e => { if (e.key === "Enter") openFile(rec); else if (e.key === " ") { e.preventDefault(); toggle(); } });
  tile.addEventListener("contextmenu", e => { e.preventDefault(); toggle(); });
  tile.addEventListener("dragstart", e => {
    if (rec.locked) return e.preventDefault();
    const ids = state.selection.has(rec.id) ? [...state.selection] : [rec.id];
    e.dataTransfer.setData("application/x-notch-ids", JSON.stringify(ids));
    if (ids.length === 1) { try { e.dataTransfer.setData("DownloadURL", `${rec.type || "application/octet-stream"}:${rec.name}:${URL.createObjectURL(rec.blob)}`); } catch { /* ok */ } }
  });
  return tile;
}

export async function removeWithUndo(ids, ctx) {
  await store.removeFiles(ids);
  ids.forEach(i => state.selection.delete(i));
  toast(ids.length === 1 ? "Moved to Recently removed" : ids.length + " files moved to Recently removed", { label: "Undo", run: async () => { await store.restoreFiles(ids); emit("files"); } });
  emit("files");
}

export async function renderFiles(ctx) {
  const page = ctx.page;
  const { live, bin, all } = await counts();
  const cats = await store.categories.list();
  const inBin = state.category === "__bin";
  const list = await visibleFiles();
  const selIds = [...state.selection].filter(id => all.some(f => f.id === id));
  state.selection = new Set(selIds);

  // categories
  const catRow = h("div", { class: "cats" },
    h("button", { class: "chip", "aria-pressed": String(state.category === ""), onclick: () => { state.category = ""; ctx.refresh(); } }, "All", h("small", {}, live)),
    ...cats.map(c => h("button", { class: "chip", "aria-pressed": String(state.category === c.id),
        onclick: async () => { if (c.locked && !vault.isOpen(c.id)) { const ok = await vault.unlock(c); if (!ok) return; } state.category = c.id; ctx.refresh(); },
        oncontextmenu: e => { e.preventDefault(); categoryMenu(c, ctx); } },
      c.locked ? h("span", { html: icon(vault.isOpen(c.id) ? "unlock" : "lock", 12) }) : null, c.name, h("small", {}, all.filter(f => !f.removedAt && f.category === c.id).length))),
    h("button", { class: "ib", "aria-label": "New category", title: "New category", html: icon("plus", 14), onclick: () => newCategory(ctx) }),
    h("span", { class: "grow" }),
    bin ? h("button", { class: "chip", "aria-pressed": String(inBin), onclick: () => { state.category = inBin ? "" : "__bin"; ctx.refresh(); } }, h("span", { html: icon("trash", 13) }), "Recently removed", h("small", {}, bin)) : null,
    h("select", { class: "field", "aria-label": "Sort", style: { height: "30px" }, onchange: e => { state.sort = e.target.value; store.prefs.set("sort", state.sort); ctx.refresh(); } },
      ...[["date", "Date"], ["name", "Name"], ["size", "Size"], ["type", "Type"]].map(([v, t]) => h("option", { value: v, selected: state.sort === v }, "Sort: " + t))));

  // actions on a selection
  const selBar = selIds.length ? h("div", { class: "selbar" },
    h("b", {}, selIds.length + " selected"), h("span", { class: "grow" }),
    ...(inBin ? [
      h("button", { class: "tbtn", onclick: async () => { await store.restoreFiles(selIds); state.selection.clear(); emit("files"); } }, "Restore"),
      h("button", { class: "tbtn", onclick: async () => { await store.deleteForever(selIds); state.selection.clear(); emit("files"); } }, "Delete now")] : [
      h("button", { class: "tbtn", onclick: () => ctx.go("tools") }, h("span", { html: icon("gear", 13) }), "Tools"),
      h("button", { class: "tbtn", onclick: () => moveMenu(selIds, cats, ctx) }, "Move to…"),
      h("button", { class: "tbtn", onclick: async () => { const recs = await Promise.all(selIds.map(i => store.files.get(i))); await store.setPinned(selIds, !recs.every(r => r.pinned)); emit("files"); } }, h("span", { html: icon("pin", 13) }), "Keep"),
      h("button", { class: "tbtn", onclick: () => saveAll(selIds) }, h("span", { html: icon("download", 13) }), "Save"),
      h("button", { class: "tbtn", onclick: () => removeWithUndo(selIds, ctx) }, h("span", { html: icon("trash", 13) }), "Remove")]),
    h("button", { class: "tbtn", onclick: () => { state.selection.clear(); ctx.refresh(); } }, "Clear")) : null;

  const body = list.length
    ? h("div", { class: "tiles" }, ...(await Promise.all(list.map(r => tileFor(r, ctx)))))
    : h("div", { class: "empty", id: "emptyDrop", onclick: () => $("#picker").click() },
        h("span", { html: icon("download", 26) }),
        h("b", {}, inBin ? "Nothing in Recently removed" : live ? "Nothing matches" : "Drop files here"),
        h("span", {}, inBin ? "Removed files stay here for 24 hours" : live ? "Try another word or category" : "or click to choose, or press Ctrl+V · kept for " + store.prefs.get("retentionDays", 3) + " days"));

  page.replaceChildren(...[catRow, selBar, body, h("p", { class: "muted", style: { marginTop: "10px" } },
    live ? "Click a file to open it · Ctrl-click or right-click to select · drag onto a category to move it" : "Everything stays in this browser. Nothing is uploaded.")].filter(Boolean));
  // dropping tiles on a category chip moves them
  $$chips(page, cats, ctx);
}

function $$chips(page, cats, ctx) {
  page.querySelectorAll(".cats .chip").forEach((chip, i) => {
    const id = i === 0 ? "" : cats[i - 1]?.id;
    if (id === undefined) return;
    chip.addEventListener("dragover", e => { if (e.dataTransfer.types.includes("application/x-notch-ids")) { e.preventDefault(); chip.classList.add("on"); } });
    chip.addEventListener("dragleave", () => chip.classList.remove("on"));
    chip.addEventListener("drop", async e => {
      chip.classList.remove("on");
      const raw = e.dataTransfer.getData("application/x-notch-ids");
      if (!raw) return;
      e.preventDefault(); e.stopPropagation();
      await store.moveTo(JSON.parse(raw), id);
      toast("Moved");
      emit("files");
    });
  });
}

async function saveAll(ids) {
  for (const id of ids) { const r = await store.files.get(id); const b = r.locked ? await vault.read(r) : r.blob; if (b) download(b, r.name); }
}

async function moveMenu(ids, cats, ctx) {
  const dlg = $("#dlg");
  dlg.replaceChildren(h("div", { class: "pal-in" }, h("h3", { style: { margin: "4px 6px 8px" } }, "Move to…"),
    h("button", { class: "pal-item", onclick: async () => { await store.moveTo(ids, ""); dlg.close(); emit("files"); } }, "No category"),
    ...cats.filter(c => !c.locked).map(c => h("button", { class: "pal-item", onclick: async () => { await store.moveTo(ids, c.id); dlg.close(); emit("files"); } }, c.name))));
  dlg.showModal();
}

async function newCategory(ctx) {
  const name = prompt("Name of the category");
  if (!name || !name.trim()) return;
  const list = await store.categories.list();
  list.push({ id: store.newId(), name: name.trim().slice(0, 40), locked: false });
  await store.categories.save(list);
  ctx.refresh();
}

async function categoryMenu(c, ctx) {
  const dlg = $("#dlg");
  const done = () => { dlg.close(); ctx.refresh(); };
  const list = await store.categories.list();
  dlg.replaceChildren(h("div", { class: "pal-in" }, h("h3", { style: { margin: "4px 6px 8px" } }, c.name),
    h("button", { class: "pal-item", onclick: async () => { const n = prompt("New name", c.name); if (n?.trim()) { list.find(x => x.id === c.id).name = n.trim(); await store.categories.save(list); } done(); } }, "Rename"),
    !c.locked ? h("button", { class: "pal-item", onclick: async () => { dlg.close(); await vault.lockCategory(c); ctx.refresh(); } }, "Lock with a password…") : null,
    c.locked && vault.isOpen(c.id) ? h("button", { class: "pal-item", onclick: async () => { vault.close(c.id); state.category = ""; done(); } }, "Close the lock") : null,
    h("button", { class: "pal-item", onclick: async () => { if (c.locked) { toast("Open and unlock it first, then move the files out"); return done(); } await store.categories.save(list.filter(x => x.id !== c.id)); for (const f of (await store.files.all()).filter(f => f.category === c.id)) { f.category = ""; await store.files.put(f); } if (state.category === c.id) state.category = ""; done(); } }, "Delete (its files stay)")));
  dlg.showModal();
}
