// Everything that adds files to the shelf comes through here: drop, paste, picker, a tool's result, a shared file.
import * as store from "./store.js";
import * as vault from "./vault.js";
import { state, emit } from "./app.js";
import { toast, formatBytes } from "./util.js";

export async function ingest(list, { notify = true } = {}) {
  list = [...list].filter(f => f && (f.size >= 0));
  if (!list.length) return [];
  const cats = await store.categories.list();
  const target = state.category && state.category !== "__bin" ? cats.find(c => c.id === state.category) : null;
  const recs = target?.locked && vault.isOpen(target.id) ? await vault.addEncrypted(target.id, list) : await store.addFiles(list, { category: target && !target.locked ? target.id : "" });
  if (notify) toast(recs.length === 1 ? recs[0].name + " added (" + formatBytes(recs[0].size) + ")" : recs.length + " files added");
  emit("files");
  return recs;
}

// Dropped items can be folders: walk them.
export async function filesFromDrop(dt) {
  const out = [];
  const walk = async (entry, path) => {
    if (entry.isFile) await new Promise(res => entry.file(f => { out.push(path ? new File([f], f.name, { type: f.type }) : f); res(); }, res));
    else if (entry.isDirectory) {
      const reader = entry.createReader();
      for (;;) { const batch = await new Promise(res => reader.readEntries(res, () => res([]))); if (!batch.length) break; for (const e of batch) await walk(e, path + entry.name + "/"); }
    }
  };
  const items = [...(dt.items || [])].map(i => i.webkitGetAsEntry?.()).filter(Boolean);
  if (items.length) for (const e of items) await walk(e, ""); else out.push(...(dt.files || []));
  return out;
}
