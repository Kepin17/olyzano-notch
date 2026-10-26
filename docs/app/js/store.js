// IndexedDB for the shelf. Files live here as Blobs; nothing leaves the browser.
import { uniqueName } from "./util.js";

const DB = "olyzano-notch-web", VER = 1;
let dbp;
const open = () => dbp || (dbp = new Promise((res, rej) => {
  const rq = indexedDB.open(DB, VER);
  rq.onupgradeneeded = () => {
    const db = rq.result;
    db.createObjectStore("files", { keyPath: "id" });
    db.createObjectStore("meta", { keyPath: "key" });
  };
  rq.onsuccess = () => res(rq.result);
  rq.onerror = () => rej(rq.error);
}));
const tx = async (store, mode, fn) => {
  const db = await open();
  return new Promise((res, rej) => {
    const t = db.transaction(store, mode), s = t.objectStore(store);
    const r = fn(s);
    t.oncomplete = () => res(r && r.result !== undefined ? r.result : undefined);
    t.onerror = () => rej(t.error);
  });
};

export const files = {
  all: () => tx("files", "readonly", s => s.getAll()),
  get: id => tx("files", "readonly", s => s.get(id)),
  put: rec => tx("files", "readwrite", s => s.put(rec)),
  del: id => tx("files", "readwrite", s => s.delete(id)),
};
export const meta = {
  get: async (key, def) => { const r = await tx("meta", "readonly", s => s.get(key)); return r ? r.value : def; },
  set: (key, value) => tx("meta", "readwrite", s => s.put({ key, value })),
};

// Settings that are small and read synchronously.
export const prefs = {
  get(key, def) { try { const v = localStorage.getItem("notch." + key); return v == null ? def : JSON.parse(v); } catch { return def; } },
  set(key, v) { try { localStorage.setItem("notch." + key, JSON.stringify(v)); } catch { /* private mode */ } },
};

export const DAY = 86400000, BIN_MS = DAY; // "Recently removed" keeps files for 24 hours, as the desktop app does

export const newId = () => (crypto.randomUUID ? crypto.randomUUID() : String(Date.now()) + Math.random().toString(16).slice(2));

// Add files (File or {name, type, blob}); returns the records. Names never overwrite another file.
export async function addFiles(list, { category = "", source = "" } = {}) {
  const taken = (await files.all()).filter(f => !f.removedAt).map(f => f.name);
  const days = prefs.get("retentionDays", 3);
  const out = [];
  for (const f of list) {
    const name = uniqueName(taken, f.name || "file");
    taken.push(name);
    const rec = { id: newId(), name, type: f.type || "", size: f.size, added: Date.now(), expires: days > 0 ? Date.now() + days * DAY : 0,
                  category, pinned: false, removedAt: 0, blob: f.blob || f, source };
    await files.put(rec);
    out.push(rec);
  }
  return out;
}

export async function removeFiles(ids) { for (const id of ids) { const r = await files.get(id); if (r) { r.removedAt = Date.now(); await files.put(r); } } }
export async function restoreFiles(ids) { for (const id of ids) { const r = await files.get(id); if (r) { r.removedAt = 0; if (r.expires) r.expires = Math.max(r.expires, Date.now() + DAY); await files.put(r); } } }
export async function deleteForever(ids) { for (const id of ids) await files.del(id); }
export async function setPinned(ids, on) { for (const id of ids) { const r = await files.get(id); if (r) { r.pinned = on; r.expires = on ? 0 : (prefs.get("retentionDays", 3) > 0 ? Date.now() + prefs.get("retentionDays", 3) * DAY : 0); await files.put(r); } } }
export async function rename(id, name) {
  const all = await files.all();
  const r = all.find(f => f.id === id);
  if (!r || !name.trim()) return;
  r.name = uniqueName(all.filter(f => f.id !== id && !f.removedAt).map(f => f.name), name.trim());
  await files.put(r);
}
export async function moveTo(ids, category) { for (const id of ids) { const r = await files.get(id); if (r) { r.category = category; await files.put(r); } } }

// Expired files go to the bin; the bin is emptied after 24 hours. Returns true if anything changed.
export async function cleanup() {
  const now = Date.now();
  let changed = false;
  for (const f of await files.all()) {
    if (f.removedAt && now - f.removedAt > BIN_MS) { await files.del(f.id); changed = true; }
    else if (!f.removedAt && !f.pinned && f.expires && f.expires < now) { f.removedAt = now; await files.put(f); changed = true; }
  }
  return changed;
}

export const categories = {
  async list() { return meta.get("categories", []); },        // [{id, name, locked, salt, check}]
  async save(list) { return meta.set("categories", list); },
};
