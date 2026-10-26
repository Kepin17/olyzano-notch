import { lazyScript, baseOf } from "../util.js";
import { decodable } from "./image.js";

// A QR code as a PNG.
export async function makeQr(text, { size = 512, level = "M" } = {}) {
  await lazyScript("vendor/qrcode.js");
  const qr = window.qrcode(0, level);
  qr.addData(text); qr.make();
  const n = qr.getModuleCount(), quiet = 4, cell = Math.max(2, Math.floor(size / (n + quiet * 2)));
  const px = (n + quiet * 2) * cell, c = new OffscreenCanvas(px, px), g = c.getContext("2d");
  g.fillStyle = "#fff"; g.fillRect(0, 0, px, px); g.fillStyle = "#000";
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) if (qr.isDark(y, x)) g.fillRect((x + quiet) * cell, (y + quiet) * cell, cell, cell);
  return c.convertToBlob({ type: "image/png" });
}
export const wifiQr = (ssid, pass, secure = true) => {
  const esc = s => s.replace(/([\;,:"])/g, "\\$1");
  return secure ? `WIFI:T:WPA;S:${esc(ssid)};P:${esc(pass)};;` : `WIFI:T:nopass;S:${esc(ssid)};;`;
};

// Read a QR code from a picture.
export async function readQr(file) {
  await lazyScript("vendor/jsQR.js");
  const bmp = await createImageBitmap(await decodable(file));
  const c = new OffscreenCanvas(bmp.width, bmp.height), g = c.getContext("2d");
  g.drawImage(bmp, 0, 0);
  const d = g.getImageData(0, 0, c.width, c.height);
  const r = window.jsQR(d.data, d.width, d.height, { inversionAttempts: "attemptBoth" });
  return r ? r.data : "";
}

export async function hashFile(file, algo = "SHA-256") {
  const d = await crypto.subtle.digest(algo, await file.arrayBuffer());
  return [...new Uint8Array(d)].map(b => b.toString(16).padStart(2, "0")).join("");
}

export async function toBase64(file) {
  const buf = new Uint8Array(await file.arrayBuffer());
  let s = ""; for (let i = 0; i < buf.length; i += 0x8000) s += String.fromCharCode(...buf.subarray(i, i + 0x8000));
  return btoa(s);
}
export function fromBase64(text, name = "decoded") {
  const clean = text.replace(/^data:[^,]*,/, "").replace(/\s+/g, "");
  const bin = atob(clean), u = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
  return { name, blob: new Blob([u]) };
}

export async function imageInfo(file) {
  const src = await decodable(file);
  const b = await createImageBitmap(src);
  const info = { Name: file.name, Type: file.type || "unknown", Size: file.size + " bytes", Dimensions: b.width + " × " + b.height + " px", Modified: file.lastModified ? new Date(file.lastModified).toLocaleString() : "" };
  b.close?.();
  return info;
}
