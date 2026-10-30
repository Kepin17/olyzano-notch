// Offline: the app shell and the vendored libraries are cached on first load; the OCR engine is one of them.
const V = "notch-web-6";
const SHELL = ["./", "index.html", "style.css", "manifest.webmanifest", "icons/notch.png", "icons/icon-maskable-512.png", "icons/traktir.jpeg",
  "js/main.js", "js/app.js", "js/util.js", "js/store.js", "js/crypto.js", "js/vault.js", "js/dialogs.js", "js/ingest.js", "js/shelf.js", "js/icons.js", "js/themes.js", "js/themes-apply.js",
  "js/palette.js", "js/settings.js", "js/tools.js", "js/tools/image.js", "js/tools/pdf.js", "js/tools/zip.js", "js/tools/ocr.js", "js/tools/misc.js", "js/workers/image.js", "js/timer.js", "js/color.js", "js/share.js", "js/music.js"];
self.addEventListener("install", e => { e.waitUntil(caches.open(V).then(c => c.addAll(SHELL)).then(() => self.skipWaiting())); });
self.addEventListener("activate", e => { e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== V).map(k => caches.delete(k)))).then(() => self.clients.claim())); });
self.addEventListener("fetch", e => {
  const url = new URL(e.request.url);
  // a file shared to the installed app from the phone's share sheet
  if (e.request.method === "POST" && url.pathname.endsWith("/share-target")) {
    // Only the system's share sheet (no Sec-Fetch-Site, or "none"/"same-origin"): a form on another website that posts files here would
    // otherwise put them on the person's shelf.
    const site = e.request.headers.get("sec-fetch-site"), from = e.request.headers.get("origin");
    if (site ? !["none", "same-origin"].includes(site) : from && from !== "null" && from !== location.origin) { e.respondWith(new Response("Not allowed", { status: 403 })); return; }
    const LIMIT = 200 * 1024 * 1024, MAX_FILES = 30;
    const safeName = n => String(n || "shared").replace(/[\u0000-\u001f\u007f]/g, "").split(/[\\/]/).pop().replace(/^\.+/, "").slice(0, 200) || "shared";
    e.respondWith((async () => {
      const form = await e.request.formData();
      const cache = await caches.open("shared");
      let total = 0;
      const files = form.getAll("files").filter(f => f instanceof File).slice(0, MAX_FILES).filter(f => (total += f.size) <= LIMIT);
      let i = 0;
      for (const f of files) { const name = safeName(f.name); await cache.put("/shared/" + Date.now() + "-" + i++ + "-" + encodeURIComponent(name), new Response(f, { headers: { "x-name": encodeURIComponent(name), "content-type": f.type } })); }
      const text = [form.get("title"), form.get("text"), form.get("url")].filter(x => typeof x === "string" && x.trim()).join("\n").slice(0, 100000);
      if (text) { files.push(new File([text], "Shared text.txt", { type: "text/plain" })); await cache.put("/shared/" + Date.now() + "-text", new Response(text, { headers: { "x-name": encodeURIComponent("Shared text.txt"), "content-type": "text/plain" } })); }
      return Response.redirect("./?shared=" + files.length, 303);
    })());
    return;
  }
  if (e.request.method !== "GET") return;
  const own = url.origin === location.origin && !url.pathname.includes("/vendor/");
  // the app's own files: the network first (so an update shows at once), the cache when offline; libraries: the cache first
  e.respondWith(own
    ? fetch(e.request, { cache: "no-cache" }).then(res => { if (res.ok) { const copy = res.clone(); caches.open(V).then(c => c.put(e.request, copy)); } return res; }).catch(() => caches.match(e.request))
    : caches.match(e.request).then(hit => hit || fetch(e.request).then(res => {
        if (res.ok && url.origin === location.origin) { const copy = res.clone(); caches.open(V).then(c => c.put(e.request, copy)); }
        return res;
      })));
});
