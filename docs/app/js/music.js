// A small player: audio files from the shelf or the disk, with synced lyrics from a .lrc file.
import { h, $, mount, clock, parseLrc } from "./util.js";
import { icon } from "./icons.js";
import * as store from "./store.js";
import { on, state } from "./app.js";

const audio = new Audio();
let track = null, lyrics = [], url = "";
audio.addEventListener("ended", () => { /* stays on the last second */ });

export async function play(file, name) {
  if (url) URL.revokeObjectURL(url);
  url = URL.createObjectURL(file);
  audio.src = url;
  track = { name: name || file.name, size: file.size };
  lyrics = [];
  try {
    const lrc = (await store.files.all()).find(f => !f.removedAt && f.name.toLowerCase() === track.name.replace(/\.[^.]+$/, "").toLowerCase() + ".lrc");
    if (lrc) lyrics = parseLrc(await lrc.blob.text());
  } catch { /* none */ }
  await audio.play().catch(() => {});
}
on("play", async rec => { await play(rec.blob, rec.name); state.tab = "music"; document.getElementById("pill")?.click(); });

export async function render(ctx) {
  const pick = h("input", { type: "file", accept: "audio/*,.lrc", multiple: true, hidden: true, onchange: async e => {
    const fs = [...e.target.files], a = fs.find(f => f.type.startsWith("audio/")), l = fs.find(f => f.name.toLowerCase().endsWith(".lrc"));
    if (l) lyrics = parseLrc(await l.text());
    if (a) { await play(a); if (l) lyrics = parseLrc(await l.text()); }
    ctx.refresh();
  } });
  const shelf = (await store.files.all()).filter(f => !f.removedAt && f.type.startsWith("audio/") && !f.locked);
  const line = h("div", { id: "lyr", style: { minHeight: "120px", textAlign: "center", padding: "14px 6px", fontSize: "20px", fontWeight: 600, lineHeight: 1.5 } });
  const seek = h("input", { type: "range", min: 0, max: 1000, value: 0, "aria-label": "Position", oninput: e => { if (audio.duration) audio.currentTime = audio.duration * e.target.value / 1000; } });
  const time = h("span", { class: "muted", style: { fontFamily: "var(--mono)" } }, "00:00 / 00:00");
  const btn = h("button", { class: "round primary", "aria-label": "Play or pause", html: icon(audio.paused ? "play" : "pause", 24), onclick: () => { audio.paused ? audio.play() : audio.pause(); } });
  const upd = () => {
    if (!document.body.contains(seek)) { audio.removeEventListener("timeupdate", upd); return; }
    seek.value = audio.duration ? audio.currentTime / audio.duration * 1000 : 0;
    time.textContent = clock(audio.currentTime * 1000) + " / " + clock((audio.duration || 0) * 1000);
    btn.innerHTML = icon(audio.paused ? "play" : "pause", 24);
    if (lyrics.length) {
      let i = lyrics.findIndex(l => l.t > audio.currentTime) - 1; if (i < -1) i = lyrics.length - 1;
      line.replaceChildren(...[-1, 0, 1].map(d => h("div", { style: { opacity: d ? .4 : 1, fontSize: d ? "14px" : "22px", transition: "all .2s" } }, lyrics[i + d]?.text || " ")));
    }
  };
  audio.addEventListener("timeupdate", upd); audio.addEventListener("play", upd); audio.addEventListener("pause", upd);
  mount(ctx.page,
    track ? h("div", { class: "card col" }, h("b", { style: { fontSize: "15px" } }, track.name), line, seek, h("div", { class: "row", style: { justifyContent: "space-between" } }, time, btn)) :
      h("div", { class: "empty" }, h("span", { html: icon("music", 26) }), h("b", {}, "Nothing is playing"), h("span", {}, "Pick a song, and a .lrc file with the same name for the words")),
    h("div", { class: "row", style: { marginTop: "10px" } }, h("button", { class: "tbtn", onclick: () => pick.click() }, h("span", { html: icon("folder", 13) }), "Choose a song (and a .lrc)…"), pick),
    shelf.length ? [h("h3", {}, "On the shelf"), h("div", { class: "flist" }, ...shelf.map(f => h("div", { class: "frow" }, h("span", { class: "n" }, f.name), h("button", { "aria-label": "Play", html: icon("play", 13), onclick: async () => { await play(f.blob, f.name); ctx.refresh(); } }))))] : null);
  upd();
}
