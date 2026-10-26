// Share: files go to other apps with the system share sheet, or to a phone through the installed app (a share target).
import { h, mount, toast, download, formatBytes, shareLink, lazyScript } from "./util.js";
import { icon } from "./icons.js";
import { state } from "./app.js";
import * as store from "./store.js";
import * as vault from "./vault.js";
import { makeQr, readQr } from "./tools/misc.js";
import { zipFiles } from "./tools/zip.js";

async function selected() {
  const out = [];
  for (const id of state.selection) {
    const r = await store.files.get(id);
    if (r && !r.removedAt) { const b = r.locked ? await vault.read(r) : r.blob; if (b) out.push(new File([b], r.name, { type: r.type })); }
  }
  return out;
}

// Receive from a computer: look at its QR code with the camera (or a photo of it); the link opens in the browser, which
// downloads and, for the relay, decrypts the files (the relay serves no other origin, so the shelf cannot fetch them itself).
async function scan() {
  const dlg = document.getElementById("dlg");
  const video = h("video", { playsinline: "", muted: "", style: { width: "100%", borderRadius: "16px", background: "#000", maxHeight: "50vh", objectFit: "cover" } });
  const msg = h("p", { class: "muted", style: { margin: "0 6px" } }, "Point the camera at the QR code on the computer.");
  const photo = h("input", { type: "file", accept: "image/*", capture: "environment", hidden: "" });
  let stream, stopped = false;
  const stop = () => { stopped = true; stream?.getTracks().forEach(t => t.stop()); };
  const found = text => { const link = shareLink(text); if (!link) { msg.textContent = "That QR code is not a link."; return false; } stop(); dlg.close(); location.href = link; return true; };
  photo.addEventListener("change", async () => { const f = photo.files[0]; if (f) found(await readQr(f).catch(() => "")) || (msg.textContent = "No QR code found in that photo."); });
  dlg.replaceChildren(h("div", { class: "pal-in col" }, video, msg,
    h("div", { class: "row", style: { justifyContent: "flex-end" } }, h("button", { class: "tbtn", onclick: () => photo.click() }, "Use a photo"), h("button", { class: "tbtn primary", onclick: () => dlg.close() }, "Cancel")), photo));
  dlg.addEventListener("close", stop, { once: true });
  dlg.showModal();
  try {
    stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment" } });
    video.srcObject = stream; await video.play();
    const detector = "BarcodeDetector" in window ? new BarcodeDetector({ formats: ["qr_code"] }) : null;
    if (!detector) await lazyScript("vendor/jsQR.js");
    const c = document.createElement("canvas"), g = c.getContext("2d", { willReadFrequently: true });
    while (!stopped) {
      await new Promise(r => setTimeout(r, 250));
      if (!video.videoWidth) continue;
      let text = "";
      if (detector) text = (await detector.detect(video).catch(() => []))[0]?.rawValue || "";
      else { c.width = video.videoWidth; c.height = video.videoHeight; g.drawImage(video, 0, 0); const d = g.getImageData(0, 0, c.width, c.height); text = window.jsQR(d.data, d.width, d.height)?.data || ""; }
      if (text && found(text)) return;
    }
  } catch { video.hidden = true; msg.textContent = "The camera is not available. Use a photo of the QR code instead."; }
}

export async function render(ctx) {
  const files = await selected();
  const url = location.href.split("?")[0].split("#")[0];
  const canShare = !!navigator.canShare && files.length && navigator.canShare({ files });
  const qr = URL.createObjectURL(await makeQr(url, { size: 320 }));
  mount(ctx.page,
    h("h3", {}, "Selected files"),
    files.length ? h("div", { class: "card col" },
      h("div", {}, h("b", {}, files.length === 1 ? files[0].name : files.length + " files"), h("span", { class: "muted" }, " · " + formatBytes(files.reduce((s, f) => s + f.size, 0)))),
      h("div", { class: "row" },
        canShare ? h("button", { class: "tbtn primary", onclick: async () => { try { await navigator.share({ files, title: files[0].name }); } catch { /* cancelled */ } } }, h("span", { html: icon("share", 14) }), "Share with an app…") : null,
        h("button", { class: "tbtn" + (canShare ? "" : " primary"), onclick: async () => { for (const f of files) download(f, f.name); } }, h("span", { html: icon("download", 14) }), "Save"),
        files.length > 1 ? h("button", { class: "tbtn", onclick: async () => { const z = await zipFiles(files, "Shared files.zip"); download(z[0].blob, z[0].name); } }, "Save as a zip") : null),
      !canShare ? h("p", { class: "muted", style: { margin: 0 } }, "This browser has no share sheet for files, so save them and send them from there.") : null)
      : h("p", { class: "muted" }, "Select files on the Files page (Ctrl-click or right-click), then come back here."),
    h("h3", {}, "Receive from a computer"),
    h("div", { class: "card row" }, h("div", { style: { flex: "1 1 220px" } }, h("b", {}, "Scan a Share via QR code"), h("p", { class: "muted", style: { margin: "4px 0 0" } }, "On the computer choose Share via QR…, then scan its code here. The files open in the browser (a PIN or password is asked there).")),
      h("button", { class: "tbtn primary", onclick: scan }, "Scan the code")),
    h("h3", {}, "Send to a phone"),
    h("div", { class: "card row", style: { alignItems: "flex-start" } },
      h("div", { class: "qrbox" }, h("img", { src: qr, alt: "QR code of this page's address", width: 136, height: 136 })),
      h("div", { style: { flex: "1 1 240px" } },
        h("b", {}, "Put this app on your phone"),
        h("ol", { class: "muted", style: { margin: "6px 0 0", paddingLeft: "18px" } },
          h("li", {}, "Scan the code and open the page."),
          h("li", {}, "Use “Add to Home screen” or “Install”."),
          h("li", {}, "From any app, choose Share → Olyzano Notch: the file lands on the shelf and the tools can work on it."))),
      ),
    h("p", { class: "muted", style: { marginTop: "10px" } }, "Nothing is uploaded to a server. The desktop app can also hand files to a phone over your Wi-Fi with a QR code; a web page cannot open a port for that."));
}
