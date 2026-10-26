import { lazyScript, extOf } from "../util.js";

const stored = new Set(["zip", "jpg", "jpeg", "png", "webp", "gif", "mp3", "mp4", "mkv", "webm", "7z", "rar", "gz", "pdf", "heic"]);
const typeByExt = { txt: "text/plain", md: "text/markdown", json: "application/json", csv: "text/csv", html: "text/html", css: "text/css", js: "text/javascript", pdf: "application/pdf",
  png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", webp: "image/webp", gif: "image/gif", svg: "image/svg+xml", mp3: "audio/mpeg", wav: "audio/wav", ogg: "audio/ogg", mp4: "video/mp4", webm: "video/webm", zip: "application/zip" };

export async function zipFiles(files, name = "Archive.zip") {
  await lazyScript("vendor/fflate.min.js");
  const tree = {};
  const seen = new Set();
  for (const f of files) {
    let n = f.name; while (seen.has(n)) n = "copy-" + n; seen.add(n);
    tree[n] = [new Uint8Array(await (f.blob || f).arrayBuffer()), { level: stored.has(extOf(f.name)) ? 0 : 6, mtime: f.lastModified ? new Date(f.lastModified) : new Date() }];
  }
  const data = window.fflate.zipSync(tree);
  return [{ name, blob: new Blob([data], { type: "application/zip" }) }];
}

export async function unzip(file) {
  await lazyScript("vendor/fflate.min.js");
  let entries;
  try { entries = window.fflate.unzipSync(new Uint8Array(await file.arrayBuffer())); } catch { throw new Error(file.name + ": this is not a readable zip"); }
  return Object.entries(entries).filter(([n]) => !n.endsWith("/")).map(([n, data]) => ({ name: n.split("/").pop(), blob: new Blob([data], { type: typeByExt[extOf(n)] || "" }) }));
}
