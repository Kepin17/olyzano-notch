// Small pure helpers, shared by the app and by tests.html.

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];
export const sleep = ms => new Promise(r => setTimeout(r, ms));

export function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === "class") el.className = v;
    else if (k === "html") el.innerHTML = v;
    else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else if (k === "style" && typeof v === "object") Object.assign(el.style, v);
    else el.setAttribute(k, v === true ? "" : v);
  }
  for (const kid of kids.flat()) if (kid != null && kid !== false) el.append(kid.nodeType ? kid : document.createTextNode(kid));
  return el;
}

export function formatBytes(n) {
  if (n >= 1073741824) return (n / 1073741824).toFixed(1) + " GB";
  if (n >= 1048576) return (n / 1048576).toFixed(n >= 10485760 ? 0 : 1) + " MB";
  return Math.max(1, Math.round(n / 1024)) + " KB";
}

export const extOf = name => { const i = name.lastIndexOf("."); return i > 0 ? name.slice(i + 1).toLowerCase() : ""; };
export const baseOf = name => { const i = name.lastIndexOf("."); return i > 0 ? name.slice(0, i) : name; };

// A name that never overwrites: "photo.jpg" -> "photo (2).jpg" when taken.
export function uniqueName(taken, name) {
  const set = new Set(taken);
  if (!set.has(name)) return name;
  const base = baseOf(name), ext = extOf(name);
  for (let i = 2; ; i++) {
    const n = base + " (" + i + ")" + (ext ? "." + ext : "");
    if (!set.has(n)) return n;
  }
}

// "2-5, 8, 10-" -> 1-based page numbers within 1..pageCount, in order, without duplicates.
// Returns {pages, error}; the same rules as DocTools::parsePageRanges in the desktop app.
export function parsePageRanges(text, pageCount) {
  const out = [], seen = new Set();
  const parts = String(text).split(",").map(s => s.trim()).filter(Boolean);
  if (!parts.length) return { pages: [], error: "Type the pages to keep, like 1-3, 5" };
  for (const p of parts) {
    const m = /^(\d*)\s*(-)?\s*(\d*)$/.exec(p);
    if (!m || (!m[1] && !m[3])) return { pages: [], error: "“" + p + "” is not a page or a range" };
    let a = m[1] ? +m[1] : 1, b = m[2] ? (m[3] ? +m[3] : pageCount) : a;
    if (a > b) [a, b] = [b, a];
    if (a < 1 || b > pageCount) return { pages: [], error: "The file has " + pageCount + (pageCount === 1 ? " page" : " pages") };
    for (let i = a; i <= b; i++) if (!seen.has(i)) { seen.add(i); out.push(i); }
  }
  return { pages: out, error: "" };
}

// "[01:23.45]text" lines -> [{t (seconds), text}] sorted by time (LrcParser.h in the desktop app).
export function parseLrc(text) {
  const lines = [];
  for (const raw of String(text).split(/\r?\n/)) {
    const stamps = [...raw.matchAll(/\[(\d+):(\d+(?:\.\d+)?)\]/g)];
    if (!stamps.length) continue;
    const body = raw.replace(/\[[^\]]*\]/g, "").trim();
    for (const s of stamps) lines.push({ t: +s[1] * 60 + parseFloat(s[2]), text: body });
  }
  return lines.sort((a, b) => a.t - b.t);
}

export function clock(ms) {
  const total = Math.max(0, Math.round(ms / 1000));
  const hh = Math.floor(total / 3600), mm = Math.floor(total / 60) % 60, ss = total % 60;
  const two = n => (n < 10 ? "0" : "") + n;
  return (hh > 0 ? hh + ":" + two(mm) : two(mm)) + ":" + two(ss);
}

export function kindOf(file) {
  const t = file.type || "", e = extOf(file.name);
  if (t.startsWith("image/") || ["heic", "heif"].includes(e)) return "image";
  if (t === "application/pdf" || e === "pdf") return "pdf";
  if (t.startsWith("audio/")) return "audio";
  if (t.startsWith("video/")) return "video";
  if (t.startsWith("text/") || ["txt", "md", "json", "csv", "log", "lrc", "html", "css", "js"].includes(e)) return "text";
  if (["zip", "7z", "rar", "gz", "tar"].includes(e)) return "archive";
  return "file";
}

export function download(blob, name) {
  const a = h("a", { href: URL.createObjectURL(blob), download: name });
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
}

// Fill an element with children, leaving out null and false (replaceChildren would print "null").
export const mount = (el, ...kids) => el.replaceChildren(...kids.flat(Infinity).filter(k => k != null && k !== false));

export function toast(msg, action) {
  const box = document.getElementById("toasts");
  if (!box) return;
  const t = h("div", { class: "toast" }, h("span", {}, msg), action ? h("button", { onclick: () => { action.run(); t.remove(); } }, action.label) : null);
  box.append(t);
  requestAnimationFrame(() => t.classList.add("on"));
  setTimeout(() => { t.classList.remove("on"); setTimeout(() => t.remove(), 300); }, action ? 6000 : 3200);
}

export function lazyScript(src) {
  lazyScript.cache = lazyScript.cache || {};
  return lazyScript.cache[src] || (lazyScript.cache[src] = new Promise((res, rej) => {
    const s = h("script", { src, onload: () => res(), onerror: () => rej(new Error("Could not load " + src)) });
    document.head.append(s);
  }));
}
