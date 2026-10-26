import { h, $, $$, toast, sleep } from "./util.js";
import { icon } from "./icons.js";
import { state, on, emit, TABS } from "./app.js";
import { applyTheme } from "./themes-apply.js";
import * as store from "./store.js";
import { ingest, filesFromDrop } from "./ingest.js";
import { renderFiles, counts } from "./shelf.js";
import { openPalette } from "./palette.js";

applyTheme();

const pages = {
  files: renderFiles,
  tools: (...a) => import("./tools.js").then(m => m.render(...a)),
  timer: (...a) => import("./timer.js").then(m => m.render(...a)),
  color: (...a) => import("./color.js").then(m => m.render(...a)),
  share: (...a) => import("./share.js").then(m => m.render(...a)),
  music: (...a) => import("./music.js").then(m => m.render(...a)),
  settings: (...a) => import("./settings.js").then(m => m.render(...a)),
};
const TITLES = { files: "Olyzano Notch", tools: "File tools", timer: "Timer", color: "Color picker", share: "Share", music: "Now playing", settings: "Settings" };

const notch = $("#notch"), pageEl = $("#page"), tabsEl = $("#tabs");
const ctx = { page: pageEl, refresh: () => render(), go: tab => go(tab) };

function setOpen(open) {
  state.open = open;
  notch.dataset.open = String(open);
  $("#pill").setAttribute("aria-expanded", String(open));
  $(".sheet").setAttribute("aria-hidden", String(!open));
  document.body.classList.toggle("panel-open", open);
  if (open) render();
}

function go(tab) {
  if (tab !== "settings" && tab !== state.tab) state.prev = state.tab;
  else if (tab === "settings" && state.tab !== "settings") state.prev = state.tab;
  state.tab = tab;
  if (!state.open) setOpen(true); else render();
}

let rendering = 0;
async function render() {
  const mine = ++rendering;
  $("#title").textContent = TITLES[state.tab] || "Olyzano Notch";
  $("#back").hidden = state.tab !== "settings";
  $("#search").hidden = state.tab !== "files";
  tabsEl.hidden = state.tab === "settings";
  $$(".tab", tabsEl).forEach(b => b.setAttribute("aria-selected", String(b.dataset.tab === state.tab)));
  $("#settingsBtn").classList.toggle("on", state.tab === "settings");
  const c = await counts();
  $("#count").hidden = state.tab !== "files"; $("#count").textContent = c.live;
  $("#pillCount").hidden = !c.live; $("#pillCount").textContent = c.live;
  if (mine !== rendering) return;
  const fn = pages[state.tab];
  try { await fn(ctx); } catch (e) { console.error(e); pageEl.replaceChildren(h("p", { class: "muted" }, "Something went wrong: " + e.message)); }
}

// header
$("#back").innerHTML = icon("back", 16);
$("#palette").innerHTML = icon("search", 15);
$("#settingsBtn").innerHTML = icon("gear", 15);
tabsEl.replaceChildren(...TABS.map(([id, name, ic]) => h("button", { class: "tab", role: "tab", "data-tab": id, "aria-selected": "false", onclick: () => go(id) }, h("span", { html: icon(ic, 14) }), name)));
$("#back").addEventListener("click", () => go(state.prev || "files"));
$("#settingsBtn").addEventListener("click", () => go(state.tab === "settings" ? state.prev || "files" : "settings"));
$("#palette").addEventListener("click", () => openPalette({ go, ctx }));
$("#pinBtn").addEventListener("click", () => { state.pinned = !state.pinned; store.prefs.set("pinned", state.pinned); syncPin(); });
const syncPin = () => { $("#pinBtn").setAttribute("aria-pressed", String(state.pinned)); $("#pinBtn").textContent = state.pinned ? "Pinned" : "Pin"; };
syncPin();
$("#search").addEventListener("input", e => { state.query = e.target.value; render(); });
$("#pill").addEventListener("click", () => setOpen(true));
$("#heroOpen").addEventListener("click", () => go("files"));
$("#heroTools").addEventListener("click", () => go("tools"));

// open on hover at the very top, close when the pointer leaves (unless pinned)
let leaveTimer;
notch.addEventListener("pointerenter", e => { clearTimeout(leaveTimer); if (e.pointerType === "mouse" && !state.open) setOpen(true); });
notch.addEventListener("pointerleave", e => { if (e.pointerType !== "mouse" || state.pinned || !state.open) return; leaveTimer = setTimeout(() => { if (!$("#dlg").open) setOpen(false); }, 700); });
addEventListener("pointermove", e => { if (e.pointerType === "mouse" && e.clientY <= 3 && !state.open) setOpen(true); }, { passive: true });
addEventListener("keydown", e => {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "k") { e.preventDefault(); openPalette({ go, ctx }); }
  else if (e.key === "Escape" && state.open && !$("#dlg").open) { if (state.selection.size) { state.selection.clear(); render(); } else if (state.tab === "settings") go(state.prev || "files"); else { state.pinned = false; syncPin(); setOpen(false); } }
  else if (e.key.toLowerCase() === "a" && (e.ctrlKey || e.metaKey) && state.open && state.tab === "files" && !/input|textarea|select/i.test(document.activeElement?.tagName)) { e.preventDefault(); import("./shelf.js").then(async m => { (await m.visibleFiles()).forEach(f => state.selection.add(f.id)); render(); }); }
});

// adding files: drop, paste, picker
let dragDepth = 0;
const hasFiles = e => [...(e.dataTransfer?.types || [])].includes("Files");
addEventListener("dragenter", e => { if (hasFiles(e)) { dragDepth++; $("#drop").classList.add("on"); document.body.classList.add("dragging"); } });
addEventListener("dragleave", e => { if (hasFiles(e) && --dragDepth <= 0) { dragDepth = 0; $("#drop").classList.remove("on"); document.body.classList.remove("dragging"); } });
addEventListener("dragover", e => { if (hasFiles(e)) e.preventDefault(); });
addEventListener("drop", async e => {
  dragDepth = 0; $("#drop").classList.remove("on"); document.body.classList.remove("dragging");
  if (!hasFiles(e)) return;
  e.preventDefault();
  if (e.target.closest?.("[data-tool-drop]")) return; // a tool takes its own files
  const list = await filesFromDrop(e.dataTransfer);
  if (state.tab === "settings") state.tab = "files";
  setOpen(true);
  await ingest(list);
});
addEventListener("paste", async e => {
  if (/input|textarea/i.test(document.activeElement?.tagName)) return;
  const items = [...(e.clipboardData?.items || [])];
  const fs = items.filter(i => i.kind === "file").map(i => i.getAsFile());
  if (fs.length) { e.preventDefault(); setOpen(true); if (state.tab !== "files") state.tab = "files"; await ingest(fs.map((f, i) => f.name && f.name !== "image.png" ? f : new File([f], "Clipboard " + new Date().toISOString().slice(0, 19).replace("T", " ").replace(/:/g, ".") + (fs.length > 1 ? " " + (i + 1) : "") + "." + (f.type.split("/")[1] || "png"), { type: f.type }))); return; }
  const text = e.clipboardData?.getData("text/plain");
  if (text && text.trim()) { setOpen(true); await ingest([new File([text], (/^https?:\/\//.test(text.trim()) ? "Link " : "Snippet ") + new Date().toISOString().slice(0, 16).replace("T", " ").replace(":", ".") + ".txt", { type: "text/plain" })]); }
});
$("#picker").addEventListener("change", async e => { const l = [...e.target.files]; e.target.value = ""; await ingest(l); });
on("files", () => render());
on("stretch", () => import("./timer.js").then(m => m.syncStretch?.()));

// housekeeping
const sweep = async () => { if (await store.cleanup()) emit("files"); };
sweep(); setInterval(sweep, 60000);
import("./timer.js").then(m => m.syncStretch?.());

// install and offline
let deferred;
addEventListener("beforeinstallprompt", e => { e.preventDefault(); deferred = e; $("#heroInstall").hidden = false; });
$("#heroInstall").addEventListener("click", async () => { if (deferred) { deferred.prompt(); deferred = null; $("#heroInstall").hidden = true; } });
if ("serviceWorker" in navigator) navigator.serviceWorker.register("sw.js").catch(() => {});
navigator.storage?.persist?.();

// first look: open the notch (pinned) on desktop, closed pill on phones
const start = new URLSearchParams(location.search).get("tab");
if (start && pages[start]) { state.tab = start; setOpen(true); }
else if (matchMedia("(min-width: 700px)").matches) { await sleep(250); setOpen(true); }
else render();
