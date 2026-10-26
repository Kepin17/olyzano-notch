import { h } from "./util.js";
export async function render(ctx) { ctx.page.replaceChildren(h("p", { class: "muted" }, "tools is on its way.")); }
