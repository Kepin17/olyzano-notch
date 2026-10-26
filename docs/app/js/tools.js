// The Tools page: image, PDF, text and archive tools that run entirely in the browser.
import { h, $, mount, formatBytes, kindOf, extOf, baseOf, download, toast, parsePageRanges } from "./util.js";
import { icon } from "./icons.js";
import { state } from "./app.js";
import * as store from "./store.js";
import * as vault from "./vault.js";
import { ingest } from "./ingest.js";
import * as image from "./tools/image.js";
import * as pdf from "./tools/pdf.js";
import * as zip from "./tools/zip.js";
import * as misc from "./tools/misc.js";

const T = { open: null, inputs: [], results: [], text: "", busy: false, progress: 0, note: "", opts: {} };

const IMG = ["image"], PDF = ["pdf"];
const TOOLS = [
  { id: "image-convert", name: "Convert images", desc: "PNG, JPG, WebP (and HEIC in)", icon: "image", accepts: IMG, group: "Images", defaults: { format: "webp", quality: 85, maxWidth: 0 } },
  { id: "image-resize", name: "Resize images", desc: "By width or into a box, never upscaled", icon: "image", accepts: IMG, group: "Images", defaults: { width: 1920, height: 0, fit: "contain", format: "same" } },
  { id: "image-compress", name: "Compress images", desc: "Aim for a file size", icon: "image", accepts: IMG, group: "Images", defaults: { format: "webp", targetKB: 300, maxWidth: 0 } },
  { id: "images-pdf", name: "Images to PDF", desc: "One page per image", icon: "folder", accepts: IMG, group: "PDF", defaults: { size: "a4" } },
  { id: "pdf-merge", name: "Merge PDFs", desc: "Join several PDFs, in your order", icon: "folder", accepts: PDF, min: 2, group: "PDF", defaults: {} },
  { id: "pdf-split", name: "Split a PDF", desc: "Every page as its own file", icon: "folder", accepts: PDF, max: 1, group: "PDF", defaults: {} },
  { id: "pdf-extract", name: "Extract pages", desc: "Keep pages like 1-3, 5, 8-", icon: "folder", accepts: PDF, max: 1, group: "PDF", defaults: { pages: "" } },
  { id: "pdf-rotate", name: "Rotate pages", desc: "Turn all or some pages", icon: "folder", accepts: PDF, group: "PDF", defaults: { degrees: 90, pages: "" } },
  { id: "pdf-compress", name: "Compress a PDF", desc: "Smaller file, pages become pictures", icon: "folder", accepts: PDF, group: "PDF", defaults: { level: "balanced" } },
  { id: "ocr", name: "Read text (OCR)", desc: "From an image or a scanned PDF", icon: "search", accepts: ["image", "pdf"], max: 1, group: "Text", defaults: { lang: "eng" } },
  { id: "zip", name: "Make a zip", desc: "Pack files into one archive", icon: "download", accepts: null, group: "Archives", defaults: { name: "Archive" } },
  { id: "unzip", name: "Open a zip", desc: "Unpack a .zip onto the shelf", icon: "download", accepts: ["archive"], max: 1, group: "Archives", defaults: {} },
  { id: "qr-make", name: "Make a QR code", desc: "Text, a link or Wi-Fi", icon: "grid", accepts: null, noInput: true, group: "More", defaults: { kind: "text", text: "", ssid: "", pass: "" } },
  { id: "qr-read", name: "Read a QR code", desc: "From a picture", icon: "grid", accepts: IMG, max: 1, group: "More", defaults: {} },
  { id: "hash", name: "File checksum", desc: "SHA-256 of a file", icon: "lock", accepts: null, group: "More", defaults: { algo: "SHA-256" } },
  { id: "base64", name: "Base64", desc: "Encode a file or decode text", icon: "copy", accepts: null, group: "More", defaults: { mode: "encode", text: "" } },
  { id: "info", name: "Image info", desc: "Size, type and dimensions", icon: "info", accepts: IMG, group: "More", defaults: {} },
];
const toolById = id => TOOLS.find(t => t.id === id);

const matches = (tool, f) => !tool.accepts || tool.accepts.includes(kindOf(f)) || (tool.accepts.includes("archive") && extOf(f.name) === "zip");

async function selectedFromShelf() {
  const out = [];
  for (const id of state.selection) {
    const r = await store.files.get(id);
    if (!r || r.removedAt) continue;
    const blob = r.locked ? await vault.read(r) : r.blob;
    if (blob) out.push({ name: r.name, size: r.size, type: r.type, blob: new File([blob], r.name, { type: r.type }), shelfId: r.id });
  }
  return out;
}

function openTool(id, ctx) {
  const t = toolById(id);
  T.open = id; T.results = []; T.text = ""; T.progress = 0; T.note = "";
  T.opts = { ...t.defaults };
  T.inputs = [];
  selectedFromShelf().then(list => { T.inputs = list.filter(f => matches(t, f)); ctx.refresh(); });
}

const addLocal = (list, tool) => { for (const f of list) if (matches(tool, f)) T.inputs.push({ name: f.name, size: f.size, type: f.type, blob: f }); };

export async function render(ctx) {
  const page = ctx.page;
  if (state.toolOpen) { openTool(state.toolOpen, ctx); state.toolOpen = null; }
  if (!T.open) return renderGrid(ctx);
  return renderTool(ctx, toolById(T.open));
}

function renderGrid(ctx) {
  const groups = [...new Set(TOOLS.map(t => t.group))];
  mount(ctx.page, 
    h("p", { class: "muted", style: { margin: "0 0 4px" } }, "Pick a tool, then choose files or use the ones selected on the shelf. Everything happens in this browser: nothing is uploaded."),
    ...groups.flatMap(g => [h("h3", {}, g), h("div", { class: "tools" }, ...TOOLS.filter(t => t.group === g).map(t =>
      h("button", { class: "tool", onclick: () => { openTool(t.id, ctx); } }, h("b", {}, h("span", { html: icon(t.icon, 15) }), t.name), h("span", {}, t.desc))))]));
}

const field = (label, control, hint) => h("div", { class: "settings-row" }, h("div", {}, h("b", {}, label), hint ? h("small", {}, hint) : null), control);
const seg = (opts, key, items, ctx) => h("div", { class: "seg" }, ...items.map(([v, t]) => h("button", { "aria-pressed": String(opts[key] === v), onclick: () => { opts[key] = v; ctx.refresh(); } }, t)));
const num = (opts, key, props = {}) => h("input", { class: "field", type: "number", style: { width: "110px" }, value: opts[key] || "", min: 0, ...props, oninput: e => { opts[key] = +e.target.value || 0; } });

async function renderTool(ctx, tool) {
  const o = T.opts, sup = await image.supportedFormats();
  const fmtItems = [["png", "PNG"], ["jpg", "JPG"], ["webp", "WebP"], ...(sup.avif ? [["avif", "AVIF"]] : [])].filter(([k]) => k === "png" || k === "jpg" || sup[k]);
  const ok = T.inputs.filter(f => matches(tool, f));
  const need = tool.min || 1;
  const picker = h("input", { type: "file", multiple: tool.max !== 1, hidden: true, accept: tool.accepts?.includes("image") && !tool.accepts.includes("pdf") ? "image/*,.heic,.heif" : tool.accepts?.includes("pdf") && tool.accepts.length === 1 ? "application/pdf" : tool.accepts?.includes("archive") ? ".zip" : "", onchange: e => { addLocal([...e.target.files], tool); if (tool.max === 1) T.inputs = T.inputs.slice(-1); ctx.refresh(); } });
  const dropZone = h("div", { class: "drop-in", "data-tool-drop": "", onclick: () => picker.click(), tabindex: "0",
    ondragover: e => { e.preventDefault(); dropZone.classList.add("over"); }, ondragleave: () => dropZone.classList.remove("over"),
    ondrop: async e => { e.preventDefault(); dropZone.classList.remove("over"); addLocal([...e.dataTransfer.files], tool); if (tool.max === 1) T.inputs = T.inputs.slice(-1); ctx.refresh(); } },
    h("span", { html: icon("download", 22) }), h("b", { style: { color: "var(--text)" } }, tool.noInput ? "" : "Drop files here or click to choose"), h("span", {}, tool.accepts ? "Accepts: " + tool.accepts.join(", ") : "Any files"));

  const list = h("div", { class: "flist" }, ...T.inputs.map((f, i) => h("div", { class: "frow" }, h("span", { class: "n", title: f.name }, f.name), h("span", { class: "s" }, formatBytes(f.size)),
    tool.id === "pdf-merge" ? [h("button", { "aria-label": "Move up", disabled: i === 0, html: icon("chevron", 13).replace("<svg", '<svg style="transform:rotate(180deg)"'), onclick: () => { [T.inputs[i - 1], T.inputs[i]] = [T.inputs[i], T.inputs[i - 1]]; ctx.refresh(); } }),
                              h("button", { "aria-label": "Move down", disabled: i === T.inputs.length - 1, html: icon("chevron", 13), onclick: () => { [T.inputs[i + 1], T.inputs[i]] = [T.inputs[i], T.inputs[i + 1]]; ctx.refresh(); } })] : null,
    h("button", { "aria-label": "Remove", html: icon("close", 12), onclick: () => { T.inputs.splice(i, 1); ctx.refresh(); } }))));

  const opts = [];
  switch (tool.id) {
    case "image-convert":
      opts.push(field("Convert to", seg(o, "format", fmtItems, ctx)));
      if (o.format !== "png") opts.push(field("Quality " + o.quality + "%", h("input", { type: "range", min: 30, max: 100, value: o.quality, style: { width: "180px" }, oninput: e => { o.quality = +e.target.value; e.target.closest(".settings-row").querySelector("b").textContent = "Quality " + o.quality + "%"; } })));
      opts.push(field("Largest width", num(o, "maxWidth", { placeholder: "keep" }), "Larger pictures are scaled down, never up. Empty keeps the size."));
      break;
    case "image-resize":
      opts.push(field("Quick sizes", h("div", { class: "row" }, ...[1920, 1280, 800, 400].map(w => h("button", { class: "chip", "aria-pressed": String(o.width === w && !o.height), onclick: () => { o.width = w; o.height = 0; ctx.refresh(); } }, w + " px")))));
      opts.push(field("Width × height", h("div", { class: "row" }, num(o, "width", { placeholder: "width" }), "×", num(o, "height", { placeholder: "height" })), "Fits inside the box, never upscaled."));
      opts.push(field("Save as", seg(o, "format", [["same", "Same"], ["png", "PNG"], ["jpg", "JPG"], ["webp", "WebP"]], ctx)));
      break;
    case "image-compress":
      opts.push(field("Save as", seg(o, "format", [["jpg", "JPG"], ["webp", "WebP"]], ctx)));
      opts.push(field("Aim for " + (o.targetKB >= 1000 ? (o.targetKB / 1000).toFixed(1) + " MB" : o.targetKB + " KB"), h("input", { type: "range", min: 30, max: 3000, step: 10, value: o.targetKB, style: { width: "180px" }, oninput: e => { o.targetKB = +e.target.value; e.target.closest(".settings-row").querySelector("b").textContent = "Aim for " + (o.targetKB >= 1000 ? (o.targetKB / 1000).toFixed(1) + " MB" : o.targetKB + " KB"); } }), "Each picture, as small as it can be under this."));
      opts.push(field("Largest width", num(o, "maxWidth", { placeholder: "keep" })));
      break;
    case "images-pdf": opts.push(field("Page size", seg(o, "size", [["a4", "A4, fitted"], ["image", "Same as image"]], ctx))); break;
    case "pdf-extract": opts.push(field("Pages to keep", h("input", { class: "field", value: o.pages, placeholder: "1-3, 5, 8-", style: { width: "180px" }, oninput: e => { o.pages = e.target.value; } }), T.inputs[0]?.pages ? "The file has " + T.inputs[0].pages + " pages." : "")); break;
    case "pdf-rotate":
      opts.push(field("Turn by", seg(o, "degrees", [[90, "90° right"], [180, "180°"], [270, "90° left"]], ctx)));
      opts.push(field("Only pages", h("input", { class: "field", value: o.pages, placeholder: "all", style: { width: "140px" }, oninput: e => { o.pages = e.target.value; } }))); break;
    case "pdf-compress": opts.push(field("Quality", seg(o, "level", [["best", "Best"], ["balanced", "Balanced"], ["small", "Smallest"]], ctx), "Pages become pictures, so the text can no longer be selected.")); break;
    case "ocr": opts.push(field("Language", h("select", { class: "field", onchange: e => { o.lang = e.target.value; } }, ...(await import("./tools/ocr.js")).LANGS.map(([v, t]) => h("option", { value: v, selected: o.lang === v }, t))), "The first time, the language data is downloaded (a few MB), then it works offline.")); break;
    case "zip": opts.push(field("Name", h("input", { class: "field", value: o.name, style: { width: "180px" }, oninput: e => { o.name = e.target.value; } }))); break;
    case "qr-make":
      opts.push(field("Make a code for", seg(o, "kind", [["text", "Text or link"], ["wifi", "Wi-Fi"]], ctx)));
      opts.push(...(o.kind === "wifi"
        ? [field("Network name", h("input", { class: "field", value: o.ssid, oninput: e => { o.ssid = e.target.value; } })), field("Password", h("input", { class: "field", value: o.pass, type: "text", oninput: e => { o.pass = e.target.value; } }), "Scanning it joins the network.")]
        : [h("textarea", { class: "field", rows: 3, placeholder: "Type text or paste a link", style: { width: "100%", height: "auto", padding: "10px 14px", borderRadius: "16px", resize: "vertical" }, oninput: e => { o.text = e.target.value; } }, o.text)]));
      break;
    case "hash": opts.push(field("Algorithm", seg(o, "algo", [["SHA-256", "SHA-256"], ["SHA-1", "SHA-1"], ["SHA-512", "SHA-512"]], ctx))); break;
    case "base64":
      opts.push(field("Mode", seg(o, "mode", [["encode", "File → text"], ["decode", "Text → file"]], ctx)));
      if (o.mode === "decode") opts.push(h("textarea", { class: "field", rows: 4, placeholder: "Paste Base64 here", style: { width: "100%", height: "auto", padding: "10px 14px", borderRadius: "16px" }, oninput: e => { o.text = e.target.value; } }, o.text));
      break;
  }

  const needsFiles = !tool.noInput && !(tool.id === "base64" && o.mode === "decode");
  const runnable = !T.busy && (!needsFiles || ok.length >= need) && (tool.id !== "qr-make" || (o.kind === "wifi" ? o.ssid : o.text.trim()));
  const sel = state.selection.size;

  mount(ctx.page, 
    h("div", { class: "row", style: { marginBottom: "8px" } }, h("button", { class: "tbtn", onclick: () => { T.open = null; ctx.refresh(); } }, h("span", { html: icon("back", 13) }), "All tools"), h("b", { style: { fontSize: "15px" } }, tool.name), h("span", { class: "muted" }, tool.desc)),
    ...(needsFiles ? [h("h3", {}, "Files" + (need > 1 ? " (at least " + need + ")" : "")), dropZone, picker, T.inputs.length ? list : null,
      sel ? h("div", { class: "row" }, h("button", { class: "tbtn", onclick: async () => { const l = (await selectedFromShelf()).filter(f => matches(tool, f)); if (tool.max === 1) T.inputs = l.slice(0, 1); else for (const f of l) if (!T.inputs.some(x => x.shelfId === f.shelfId)) T.inputs.push(f); ctx.refresh(); } }, "Use the " + sel + " selected on the shelf")) : null] : []),
    opts.length ? h("h3", {}, "Options") : null, ...opts,
    h("div", { class: "row", style: { marginTop: "12px" } },
      h("button", { class: "tbtn primary", disabled: !runnable, onclick: () => run(ctx, tool) }, T.busy ? "Working…" : tool.id === "base64" ? "Go" : "Start"),
      T.note ? h("span", { class: "muted" }, T.note) : null),
    T.busy ? h("div", { class: "progress" }, h("i", { style: { width: Math.round(T.progress * 100) + "%" } })) : null,
    T.text ? h("div", {}, h("h3", {}, "Result"), h("div", { class: "out", id: "toolText" }, T.text),
      h("div", { class: "row", style: { marginTop: "8px" } },
        h("button", { class: "tbtn", onclick: async () => { await navigator.clipboard.writeText(T.text); toast("Copied"); } }, h("span", { html: icon("copy", 13) }), "Copy"),
        h("button", { class: "tbtn", onclick: async () => { await ingest([new File([T.text], "Text from " + (T.inputs[0]?.name ? baseOf(T.inputs[0].name) : "OCR") + ".txt", { type: "text/plain" })]); } }, "Save as a note on the shelf"))) : null,
    T.results.length ? h("div", {}, h("h3", {}, "Done"),
      ...T.results.map(r => h("div", { class: "result" }, h("span", { class: "n", title: r.name }, r.name), r.note ? h("span", { class: "muted" }, r.note) : null, h("span", { class: "muted" }, formatBytes(r.blob.size)),
        h("button", { class: "tbtn", onclick: () => download(r.blob, r.name) }, h("span", { html: icon("download", 13) }), "Save"),
        h("button", { class: "tbtn", onclick: async e => { await ingest([new File([r.blob], r.name, { type: r.blob.type })]); e.target.closest(".tbtn").disabled = true; } }, "Add to shelf"))),
      T.results.length > 1 ? h("div", { class: "row", style: { marginTop: "8px" } },
        h("button", { class: "tbtn", onclick: () => ingest(T.results.map(r => new File([r.blob], r.name, { type: r.blob.type }))) }, "Add all to the shelf"),
        h("button", { class: "tbtn", onclick: async () => { const z = await zip.zipFiles(T.results, "Results.zip"); download(z[0].blob, z[0].name); } }, "Save all as a zip")) : null) : null);
}

function setProgress(ctx, p, note) { T.progress = p; if (note != null) T.note = note; const bar = document.querySelector(".progress i"); if (bar) bar.style.width = Math.round(p * 100) + "%"; }

async function run(ctx, tool) {
  const o = T.opts, ins = T.inputs.filter(f => matches(tool, f)).map(f => f.blob);
  T.busy = true; T.results = []; T.text = ""; T.note = ""; T.progress = 0; ctx.refresh();
  const t0 = performance.now();
  try {
    let out = [];
    const each = async fn => { for (let i = 0; i < ins.length; i++) { setProgress(ctx, i / ins.length, ins[i].name); out.push(...[].concat(await fn(ins[i], i))); } };
    switch (tool.id) {
      case "image-convert": await each(async f => { const r = await image.transform(f, { format: o.format, quality: o.quality / 100, maxWidth: o.maxWidth || 0 }); return { ...r, note: r.srcWidth + "×" + r.srcHeight + " · " + formatBytes(r.before) + " → " + formatBytes(r.after) }; }); break;
      case "image-resize": await each(async f => {
        const fmt = o.format === "same" ? ({ png: "png", jpg: "jpg", jpeg: "jpg", webp: "webp" }[extOf(f.name)] || "png") : o.format;
        if (!o.width && !o.height) throw new Error("Type a width or a height");
        const r = await image.transform(f, { format: fmt, quality: .92, width: o.width || 0, height: o.height || 0, fit: "contain", suffix: "-" + (o.width || "") + (o.height ? "x" + o.height : "") });
        return { ...r, note: r.srcWidth + "×" + r.srcHeight + " → " + r.width + "×" + r.height };
      }); break;
      case "image-compress": await each(async f => { const r = await image.compressTo(f, o.format, o.targetKB * 1000, o.maxWidth || 0); return { ...r, note: formatBytes(r.before) + " → " + formatBytes(r.after) }; }); break;
      case "images-pdf": out = await pdf.imagesToPdf(ins, { size: o.size }); break;
      case "pdf-merge": out = await pdf.merge(ins); break;
      case "pdf-split": out = await pdf.split(ins[0]); break;
      case "pdf-extract": { const n = await pdf.pageCount(ins[0]); const r = parsePageRanges(o.pages, n); if (r.error) throw new Error(r.error); out = await pdf.extract(ins[0], r.pages); break; }
      case "pdf-rotate": { let pages; if (o.pages.trim()) { const n = await pdf.pageCount(ins[0]); const r = parsePageRanges(o.pages, n); if (r.error) throw new Error(r.error); pages = r.pages; } await each(f => pdf.rotate(f, o.degrees, pages)); break; }
      case "pdf-compress": { const lv = { best: { dpi: 200, quality: .8 }, balanced: { dpi: 150, quality: .7 }, small: { dpi: 110, quality: .55 } }[o.level]; await each(async (f) => { const r = await pdf.compress(f, lv, p => setProgress(ctx, p)); return r.map(x => ({ ...x, note: formatBytes(f.size) + " → " + formatBytes(x.blob.size) })); }); break; }
      case "ocr": { const { recognize } = await import("./tools/ocr.js"); T.text = (await recognize(ins[0], o.lang, (p, s) => setProgress(ctx, p ?? T.progress, s ? s.replace(/_/g, " ") : null))) || "(no text found)"; break; }
      case "zip": out = await zip.zipFiles(T.inputs.filter(f => matches(tool, f)).map(f => ({ name: f.name, blob: f.blob })), (o.name || "Archive").replace(/\.zip$/i, "") + ".zip"); break;
      case "unzip": out = await zip.unzip(ins[0]); break;
      case "qr-make": out = [{ name: "QR code.png", blob: await misc.makeQr(o.kind === "wifi" ? misc.wifiQr(o.ssid, o.pass, !!o.pass) : o.text.trim()) }]; break;
      case "qr-read": T.text = (await misc.readQr(ins[0])) || "No QR code found in this picture."; break;
      case "hash": T.text = (await Promise.all(ins.map(async f => (await misc.hashFile(f, o.algo)) + "  " + f.name))).join("\n"); break;
      case "base64": if (o.mode === "encode") T.text = await misc.toBase64(ins[0]); else out = [misc.fromBase64(o.text, "decoded")]; break;
      case "info": T.text = (await Promise.all(ins.map(async f => Object.entries(await misc.imageInfo(f)).filter(([, v]) => v).map(([k, v]) => k + ": " + v).join("\n")))).join("\n\n"); break;
    }
    T.results = out;
    T.note = out.length || T.text ? "Done in " + ((performance.now() - t0) / 1000).toFixed(1) + " s" : "";
    if (out.length) toast(out.length === 1 ? out[0].name + " is ready" : out.length + " files are ready");
  } catch (e) {
    console.error(e); T.note = ""; toast(e.message || "That did not work");
  } finally { T.busy = false; ctx.refresh(); }
}
