// Offline: the app shell and the vendored libraries are cached on first load; OCR data from the CDN is cached as it is used.
const V = "notch-web-1";
const SHELL = ["./", "index.html", "style.css", "manifest.webmanifest", "icons/notch.png", "icons/traktir.jpeg",
  "js/main.js", "js/app.js", "js/util.js", "js/store.js", "js/crypto.js", "js/vault.js", "js/dialogs.js", "js/ingest.js", "js/shelf.js", "js/icons.js", "js/themes.js", "js/themes-apply.js",
  "js/palette.js", "js/settings.js", "js/tools.js", "js/tools/image.js", "js/tools/pdf.js", "js/tools/zip.js", "js/tools/ocr.js", "js/tools/misc.js", "js/workers/image.js", "js/timer.js", "js/color.js", "js/share.js", "js/music.js"];
self.addEventListener("install", e => { e.waitUntil(caches.open(V).then(c => c.addAll(SHELL)).then(() => self.skipWaiting())); });
self.addEventListener("activate", e => { e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== V).map(k => caches.delete(k)))).then(() => self.clients.claim())); });
self.addEventListener("fetch", e => {
  const url = new URL(e.request.url);
  // a file shared to the installed app from the phone's share sheet
  if (e.request.method === "POST" && url.pathname.endsWith("/share-target")) {
    e.respondWith((async () => {
      const form = await e.request.formData();
      const cache = await caches.open("shared");
      const files = form.getAll("files");
      for (const f of files) await cache.put("/shared/" + Date.now() + "-" + encodeURIComponent(f.name), new Response(f, { headers: { "x-name": encodeURIComponent(f.name), "content-type": f.type } }));
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
        if (res.ok && (url.origin === location.origin || /jsdelivr|unpkg/.test(url.host))) { const copy = res.clone(); caches.open(V).then(c => c.put(e.request, copy)); }
        return res;
      })));
});
