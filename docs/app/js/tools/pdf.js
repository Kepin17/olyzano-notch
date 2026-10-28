// PDF tools with pdf-lib (build and edit) and PDF.js (render, for compressing and OCR). Everything stays in the browser.
import { lazyScript, baseOf, extOf } from "../util.js";
import { decodable } from "./image.js";

const lib = async () => { await lazyScript("vendor/pdf-lib.min.js"); return window.PDFLib; };
export async function pdfjs() {
  await lazyScript("vendor/pdf.min.js");
  window.pdfjsLib.GlobalWorkerOptions.workerSrc = "vendor/pdf.worker.min.js";
  return window.pdfjsLib;
}
const bytes = async f => new Uint8Array(await f.arrayBuffer());
const blobOf = b => new Blob([b], { type: "application/pdf" });
const load = async (PDFLib, f) => { try { return await PDFLib.PDFDocument.load(await bytes(f), { ignoreEncryption: false }); } catch (e) { throw new Error(f.name + ": " + (/encrypted/i.test(e.message) ? "this PDF is password-protected" : "this is not a readable PDF")); } };

export async function pageCount(file) { const L = await lib(); return (await load(L, file)).getPageCount(); }

// One page per image, fitted on A4 (or the page takes the image's size).
export async function imagesToPdf(files, { size = "a4" } = {}) {
  const L = await lib(), doc = await L.PDFDocument.create();
  for (const f of files) {
    const src = await decodable(f);
    let img;
    if (src.type === "image/jpeg") img = await doc.embedJpg(await bytes(src));
    else if (src.type === "image/png") img = await doc.embedPng(await bytes(src));
    else { // gif, webp, bmp...: through a canvas
      const bmp = await createImageBitmap(src), c = new OffscreenCanvas(bmp.width, bmp.height);
      c.getContext("2d").drawImage(bmp, 0, 0);
      img = await doc.embedPng(new Uint8Array(await (await c.convertToBlob({ type: "image/png" })).arrayBuffer()));
    }
    if (size === "image") { doc.addPage([img.width, img.height]).drawImage(img, { x: 0, y: 0, width: img.width, height: img.height }); continue; }
    const W = 595.28, H = 841.89, m = 28, k = Math.min((W - 2 * m) / img.width, (H - 2 * m) / img.height, 1 * 3);
    const w = img.width * k, hh = img.height * k;
    doc.addPage([W, H]).drawImage(img, { x: (W - w) / 2, y: (H - hh) / 2, width: w, height: hh });
  }
  return [{ name: files.length === 1 ? baseOf(files[0].name) + ".pdf" : "Images (" + files.length + ").pdf", blob: blobOf(await doc.save()) }];
}

export async function merge(files) {
  const L = await lib(), out = await L.PDFDocument.create();
  for (const f of files) { const d = await load(L, f); (await out.copyPages(d, d.getPageIndices())).forEach(p => out.addPage(p)); }
  return [{ name: "Merged (" + files.length + " PDFs).pdf", blob: blobOf(await out.save()) }];
}

export async function split(file) {
  const L = await lib(), d = await load(L, file), n = d.getPageCount(), pad = String(n).length, outs = [];
  for (let i = 0; i < n; i++) {
    const o = await L.PDFDocument.create();
    o.addPage((await o.copyPages(d, [i]))[0]);
    outs.push({ name: baseOf(file.name) + " - page " + String(i + 1).padStart(pad, "0") + ".pdf", blob: blobOf(await o.save()) });
  }
  return outs;
}

export async function extract(file, pages) {
  const L = await lib(), d = await load(L, file), o = await L.PDFDocument.create();
  (await o.copyPages(d, pages.map(p => p - 1))).forEach(p => o.addPage(p));
  return [{ name: baseOf(file.name) + " (pages " + pages[0] + (pages.length > 1 ? "…" + pages[pages.length - 1] : "") + ").pdf", blob: blobOf(await o.save()) }];
}

export async function rotate(file, degrees, pages) {
  const L = await lib(), d = await load(L, file), all = d.getPages();
  all.forEach((p, i) => { if (!pages || pages.includes(i + 1)) p.setRotation(L.degrees((p.getRotation().angle + degrees) % 360)); });
  return [{ name: baseOf(file.name) + " (rotated).pdf", blob: blobOf(await d.save()) }];
}

export async function reorder(file, order) { // order: 1-based page numbers, in the new order
  const L = await lib(), d = await load(L, file), o = await L.PDFDocument.create();
  (await o.copyPages(d, order.map(p => p - 1))).forEach(p => o.addPage(p));
  return [{ name: baseOf(file.name) + " (reordered).pdf", blob: blobOf(await o.save()) }];
}

// Render page `n` (1-based) to a canvas at `scale` (1 = 72 dpi).
export async function renderPage(doc, n, scale) {
  const page = await doc.getPage(n), vp = page.getViewport({ scale });
  const c = document.createElement("canvas");
  c.width = Math.ceil(vp.width); c.height = Math.ceil(vp.height);
  await page.render({ canvasContext: c.getContext("2d"), viewport: vp }).promise;
  return c;
}
export async function openPdf(file) { const P = await pdfjs(); return P.getDocument({ data: await bytes(file), isEvalSupported: false }).promise; } // CVE-2024-4367: no eval of font code from the file

// Smaller file: every page is re-drawn as a JPEG picture (so the text can no longer be selected).
export async function compress(file, { dpi = 150, quality = .7 } = {}, progress = () => {}) {
  const L = await lib(), src = await openPdf(file), o = await L.PDFDocument.create();
  for (let i = 1; i <= src.numPages; i++) {
    const page = await src.getPage(i), vp1 = page.getViewport({ scale: 1 });
    const c = await renderPage(src, i, dpi / 72);
    const jpg = await o.embedJpg(new Uint8Array(await (await new Promise(r => c.toBlob(r, "image/jpeg", quality))).arrayBuffer()));
    o.addPage([vp1.width, vp1.height]).drawImage(jpg, { x: 0, y: 0, width: vp1.width, height: vp1.height });
    progress(i / src.numPages);
    await new Promise(r => setTimeout(r));
  }
  return [{ name: baseOf(file.name) + " (small).pdf", blob: blobOf(await o.save()) }];
}
