import { h } from "./util.js";
export async function render(ctx) { ctx.page.replaceChildren(h("p", { class: "muted" }, "color is on its way.")); }
