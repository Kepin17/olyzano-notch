import { h, $ } from "./util.js";

// Ask for a password (twice when creating one). Resolves to the text, or null when cancelled.
export function askPassword({ title, confirm = false, warning = "" }) {
  const dlg = $("#dlg");
  return new Promise(resolve => {
    const a = h("input", { class: "field", type: "password", placeholder: "Password", autocomplete: "off", style: { width: "100%" } });
    const b = confirm ? h("input", { class: "field", type: "password", placeholder: "Type it again", autocomplete: "off", style: { width: "100%" } }) : null;
    const err = h("p", { class: "muted", style: { color: "var(--danger)", margin: "0", minHeight: "1.2em" } });
    let done = false;
    const finish = v => { if (done) return; done = true; dlg.close(); resolve(v); };
    const go = () => {
      if (a.value.length < (confirm ? 6 : 1)) return (err.textContent = confirm ? "Use at least 6 characters." : "Type the password.");
      if (b && a.value !== b.value) return (err.textContent = "The two do not match.");
      finish(a.value);
    };
    a.addEventListener("keydown", e => { if (e.key === "Enter") (b ? b.focus() : go()); });
    b?.addEventListener("keydown", e => { if (e.key === "Enter") go(); });
    dlg.replaceChildren(h("div", { class: "pal-in col" },
      h("h3", { style: { margin: "4px 6px 0", textTransform: "none", letterSpacing: 0, fontSize: "15px", color: "var(--text)" } }, title),
      warning ? h("p", { class: "muted", style: { margin: "0 6px" } }, warning) : null, a, b, err,
      h("div", { class: "row", style: { justifyContent: "flex-end" } }, h("button", { class: "tbtn", onclick: () => finish(null) }, "Cancel"), h("button", { class: "tbtn primary", onclick: go }, "OK"))));
    dlg.addEventListener("close", () => finish(null), { once: true });
    dlg.showModal();
    a.focus();
  });
}
