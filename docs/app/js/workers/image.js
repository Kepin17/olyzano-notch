// Image work off the main thread: decode, scale, re-encode. Receives {blob, type, quality, maxW, maxH, bg}.
self.onmessage = async e => {
  const { id, blob, type, quality, width, height, maxWidth, fit, bg } = e.data;
  try {
    const src = await createImageBitmap(blob);
    let w = src.width, h = src.height;
    if (width || height) {                       // exact box
      if (fit === "stretch") { w = width || w; h = height || h; }
      else { const r = Math.min(width ? width / w : Infinity, height ? height / h : Infinity); if (r < 1) { w = Math.round(w * r); h = Math.round(h * r); } }
    } else if (maxWidth && maxWidth < w) {       // never upscale
      h = Math.round(h * maxWidth / w); w = maxWidth;
    }
    const c = new OffscreenCanvas(Math.max(1, w), Math.max(1, h));
    const g = c.getContext("2d");
    g.imageSmoothingQuality = "high";
    if (type === "image/jpeg" || bg) { g.fillStyle = bg || "#ffffff"; g.fillRect(0, 0, c.width, c.height); } // JPEG has no transparency
    g.drawImage(src, 0, 0, c.width, c.height);
    const out = await c.convertToBlob({ type, quality });
    self.postMessage({ id, blob: out, width: c.width, height: c.height, srcWidth: src.width, srcHeight: src.height });
  } catch (err) { self.postMessage({ id, error: String(err && err.message || err) }); }
};
