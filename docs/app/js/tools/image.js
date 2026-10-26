// Image tools: convert, resize, compress. Decoding and encoding run in a worker (OffscreenCanvas), HEIC is decoded first.
import { lazyScript, extOf, baseOf } from "../util.js";

let worker, seq = 0;
const waiting = new Map();
function getWorker() {
  if (worker) return worker;
  worker = new Worker("js/workers/image.js");
  worker.onmessage = e => { const w = waiting.get(e.data.id); if (w) { waiting.delete(e.data.id); e.data.error ? w.rej(new Error(e.data.error)) : w.res(e.data); } };
  return worker;
}
const call = msg => new Promise((res, rej) => { const id = ++seq; waiting.set(id, { res, rej }); getWorker().postMessage({ id, ...msg }); });

export const FORMATS = { png: ["image/png", "PNG", "png"], jpg: ["image/jpeg", "JPG", "jpg"], webp: ["image/webp", "WebP", "webp"], avif: ["image/avif", "AVIF", "avif"] };

// Which formats this browser can write (AVIF mostly cannot).
let supported;
export async function supportedFormats() {
  if (supported) return supported;
  supported = {};
  for (const [k, [mime]] of Object.entries(FORMATS)) {
    try { const c = new OffscreenCanvas(1, 1); c.getContext("2d").fillRect(0, 0, 1, 1); const b = await c.convertToBlob({ type: mime }); supported[k] = b.type === mime; } catch { supported[k] = false; }
  }
  return supported;
}

export const isHeic = f => ["heic", "heif"].includes(extOf(f.name)) || /image\/hei[cf]/.test(f.type || "");

// A blob the browser can decode.
export async function decodable(file) {
  if (!isHeic(file)) return file;
  await lazyScript("vendor/heic2any.min.js");
  const out = await window.heic2any({ blob: file, toType: "image/png" });
  return Array.isArray(out) ? out[0] : out;
}

// opts: {format: "png"|"jpg"|"webp", quality 0-1, maxWidth, width, height, fit}
export async function transform(file, opts) {
  const [mime, , ext] = FORMATS[opts.format];
  const blob = await decodable(file);
  const r = await call({ blob, type: mime, quality: opts.quality ?? .92, maxWidth: opts.maxWidth, width: opts.width, height: opts.height, fit: opts.fit });
  if (r.blob.type !== mime) throw new Error("This browser cannot write " + FORMATS[opts.format][1] + " files");
  const suffix = opts.suffix || "";
  return { name: baseOf(file.name) + suffix + "." + ext, blob: r.blob, width: r.width, height: r.height, srcWidth: r.srcWidth, srcHeight: r.srcHeight, before: file.size, after: r.blob.size };
}

// Smallest quality that stays under `targetBytes` (a search on the encoder), or the lowest tried.
export async function compressTo(file, format, targetBytes, maxWidth) {
  let lo = .2, hi = .95, best = null;
  for (let i = 0; i < 6; i++) {
    const q = (lo + hi) / 2;
    const r = await transform(file, { format, quality: q, maxWidth, suffix: "-small" });
    if (r.after <= targetBytes) { best = r; lo = q; } else hi = q;
    if (best && hi - lo < .04) break;
  }
  return best || (await transform(file, { format, quality: .2, maxWidth, suffix: "-small" }));
}

export async function imageSize(file) {
  const b = await createImageBitmap(await decodable(file));
  const out = { width: b.width, height: b.height };
  b.close?.();
  return out;
}
