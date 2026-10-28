import { lazyScript, extOf } from "../util.js";

const stored = new Set(["zip", "jpg", "jpeg", "png", "webp", "gif", "mp3", "mp4", "mkv", "webm", "7z", "rar", "gz", "pdf", "heic"]);
const typeByExt = { txt: "text/plain", md: "text/markdown", json: "application/json", csv: "text/csv", html: "text/html", css: "text/css", js: "text/javascript", pdf: "application/pdf",
  png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", webp: "image/webp", gif: "image/gif", svg: "image/svg+xml", mp3: "audio/mpeg", wav: "audio/wav", ogg: "audio/ogg", mp4: "video/mp4", webm: "video/webm", zip: "application/zip" };

// An entry is a plain file name: no folders, no "..", no drive letters, nothing a careless unzip would write outside its target folder.
export const entryName = name => String(name || "").replace(/[\u0000-\u001f\u007f]/g, "").split(/[\\/]/).filter(x => x && x !== "." && x !== "..").pop()?.replace(/^[A-Za-z]:/, "") || "file";

export async function zipFiles(files, name = "Archive.zip") {
  await lazyScript("vendor/fflate.min.js");
  const tree = {};
  const seen = new Set();
  for (const f of files) {
    let n = entryName(f.name); while (seen.has(n)) n = "copy-" + n; seen.add(n);
    tree[n] = [new Uint8Array(await (f.blob || f).arrayBuffer()), { level: stored.has(extOf(f.name)) ? 0 : 6, mtime: f.lastModified ? new Date(f.lastModified) : new Date() }];
  }
  const data = window.fflate.zipSync(tree);
  return [{ name, blob: new Blob([data], { type: "application/zip" }) }];
}

const MAX_UNPACKED = 1024 * 1024 * 1024;
export async function unzip(file) {
  await lazyScript("vendor/fflate.min.js");
  let entries;
  let seen = 0;
  // fflate reads an entry's size from its header: refuse what would unpack to something huge (a zip bomb) before it is inflated
  const filter = f => { seen += f.originalSize; if (seen > MAX_UNPACKED) throw new Error("too big"); return true; };
  try { entries = window.fflate.unzipSync(new Uint8Array(await file.arrayBuffer()), { filter }); } catch (e) {
    if (e.message === "too big") throw new Error(file.name + ": this zip unpacks to more than " + (MAX_UNPACKED >> 20) + " MB"); throw new Error(file.name + ": this is not a readable zip"); }
  return Object.entries(entries).filter(([n]) => !n.endsWith("/")).map(([n, data]) => ({ name: entryName(n), blob: new Blob([data], { type: typeByExt[extOf(n)] || "" }) }));
}
