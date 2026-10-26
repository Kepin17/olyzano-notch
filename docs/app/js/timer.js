import { h } from "./util.js";
export async function render(ctx) { ctx.page.replaceChildren(h("p", { class: "muted" }, "timer is on its way.")); }
