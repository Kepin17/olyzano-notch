import { h, $, mount, formatBytes, toast } from "./util.js";
import { THEMES } from "./themes.js";
import { applyTheme } from "./themes-apply.js";
import { prefs } from "./store.js";
import * as store from "./store.js";
import { state, emit } from "./app.js";

export async function render(ctx) {
  const page = ctx.page;
  const theme = prefs.get("theme", "classic"), mode = prefs.get("mode", "dark");
  const est = navigator.storage?.estimate ? await navigator.storage.estimate() : null;
  const persisted = navigator.storage?.persisted ? await navigator.storage.persisted() : false;
  const set = (k, v) => { prefs.set(k, v); applyTheme(); ctx.refresh(); };
  const row = (title, sub, control) => h("div", { class: "settings-row" }, h("div", {}, h("b", {}, title), sub ? h("small", {}, sub) : null), control);

  mount(page, 
    h("h3", {}, "Theme"),
    h("div", { class: "themes" }, ...THEMES.map(t => {
      const p = t[mode === "light" ? "light" : mode === "system" ? (matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark") : "dark"];
      return h("button", { class: "theme", "aria-pressed": String(theme === t.id), title: t.caption, style: { background: p.island, "--ts": p.surface, "--ta": p.accent, "--tt": p.text }, onclick: () => set("theme", t.id) }, h("i"), h("b", {}, t.name));
    })),
    h("p", { class: "muted", style: { margin: "8px 0" } }, THEMES.find(t => t.id === theme)?.caption || ""),
    h("div", { class: "row" }, h("div", { class: "seg" }, ...[["dark", "Dark"], ["light", "Light"], ["system", "Follow system"]].map(([v, t]) => h("button", { "aria-pressed": String(mode === v), onclick: () => set("mode", v) }, t)))),

    h("h3", {}, "Shelf"),
    row("Keep files for", "Files you do not pin go to Recently removed after this, and are deleted a day later.",
      h("select", { class: "field", onchange: e => { prefs.set("retentionDays", +e.target.value); toast("New files are kept " + (+e.target.value ? e.target.value + " days" : "until you remove them")); } },
        ...[1, 3, 7, 14, 30, 0].map(d => h("option", { value: d, selected: prefs.get("retentionDays", 3) === d }, d ? d + (d === 1 ? " day" : " days") : "Until I remove them")))),
    row("Storage", est ? formatBytes(est.usage || 0) + " used of about " + formatBytes(est.quota || 0) + (persisted ? " · protected from clean-up" : " · the browser may clear it when space is low") : "The browser does not report it",
      !persisted && navigator.storage?.persist ? h("button", { class: "tbtn", onclick: async () => { toast((await navigator.storage.persist()) ? "Protected from clean-up" : "The browser said no"); ctx.refresh(); } }, "Keep my files") : null),
    row("Empty the shelf", "Removes every file from this browser.", h("button", { class: "tbtn", onclick: async () => { if (confirm("Delete every file on the shelf?")) { for (const f of await store.files.all()) await store.files.del(f.id); emit("files"); toast("Shelf emptied"); } } }, "Empty…")),

    h("h3", {}, "Notifications"),
    row("Timer and stretch alerts", "Sound and a notification when a timer ends or it is time to stretch.",
      h("button", { class: "tbtn", onclick: async () => { toast("Notifications: " + (await Notification.requestPermission())); } }, "Allow")),
    row("Stretch reminder", "Ask me to take a break after working for a while with this tab open.",
      h("label", { class: "switch" }, h("input", { type: "checkbox", checked: prefs.get("stretch", false), onchange: e => { prefs.set("stretch", e.target.checked); emit("stretch"); } }))),

    h("h3", {}, "About"),
    h("div", { class: "card about" },
      h("img", { src: "icons/notch.png", width: 56, height: 56, alt: "" }),
      h("div", { style: { flex: "1 1 220px" } }, h("b", { style: { fontSize: "16px" } }, "Olyzano Notch for the web"),
        h("div", { class: "muted" }, "Made by Kevien.OJ (Kepin17) · MIT License"),
        h("div", { class: "row", style: { marginTop: "8px" } },
          h("a", { class: "tbtn", href: "../", target: "_blank", rel: "noopener" }, "Desktop app and demo"),
          h("a", { class: "tbtn", href: "https://github.com/Kepin17/olyzano-notch", target: "_blank", rel: "noopener" }, "Source"),
          h("a", { class: "tbtn", href: "https://github.com/Kepin17/olyzano-notch/issues", target: "_blank", rel: "noopener" }, "Report a problem"))),
      h("div", { style: { flex: "1 1 100%" } }, h("b", {}, "Contribute"),
        h("p", { class: "muted", style: { margin: "2px 0 8px" } }, "Open source and open to help: code, translations, ideas, bug reports. Fork it, make your change on a branch, send a pull request."),
        h("div", { class: "row" },
          h("a", { class: "tbtn primary", href: "https://github.com/Kepin17/olyzano-notch#contributing", target: "_blank", rel: "noopener" }, "How to contribute"),
          h("a", { class: "tbtn", href: "https://github.com/Kepin17/olyzano-notch/blob/main/docs/developer-guide.md", target: "_blank", rel: "noopener" }, "Developer guide"),
          h("a", { class: "tbtn", href: "https://github.com/Kepin17/olyzano-notch/pulls", target: "_blank", rel: "noopener" }, "Send a pull request"))),
      h("div", { class: "qrbox", title: "Buy me a coffee: scan with your banking or e-wallet app" }, h("img", { src: "icons/traktir.jpeg", alt: "QR code to buy the author a coffee" })),
      h("p", { class: "muted", style: { flex: "1 1 100%", margin: 0 } }, "Buy me a coffee: scan the QR code with your banking or e-wallet app. Thank you!")),
    h("p", { class: "muted", style: { marginTop: "10px" } }, "A web page cannot do what the desktop app does with your system: the status bar, tray, Quick Settings, KDE Connect, the dock, gaming mode and watching your screenshots folder are desktop only."));
}
