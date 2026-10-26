// Locked categories: their files are encrypted in IndexedDB; the key lives in memory while the lock is open.
import * as store from "./store.js";
import { askPassword } from "./dialogs.js";
import { deriveKey, encryptBytes, decryptBytes, makeCheck, checkKey, newSalt } from "./crypto.js";
import { toast } from "./util.js";
import { state } from "./app.js";

const keys = new Map();
export const isOpen = id => keys.has(id);
export const close = id => keys.delete(id);

export async function unlock(cat) {
  const pw = await askPassword({ title: "Unlock “" + cat.name + "”" });
  if (pw == null) return false;
  const key = await deriveKey(pw, cat.salt);
  if (!(await checkKey(key, cat.check))) { toast("That is not the password"); return false; }
  keys.set(cat.id, key);
  return true;
}

export async function lockCategory(cat) {
  const pw = await askPassword({ title: "Lock “" + cat.name + "”", confirm: true,
    warning: "There is no way to get the files back if you forget this password. They are encrypted in this browser with AES-256." });
  if (pw == null) return;
  const salt = newSalt(), key = await deriveKey(pw, salt);
  for (const f of (await store.files.all()).filter(f => f.category === cat.id && !f.locked)) {
    const enc = await encryptBytes(key, await f.blob.arrayBuffer());
    f.blob = new Blob([enc.data]); f.iv = enc.iv; f.locked = true;
    await store.files.put(f);
  }
  const list = await store.categories.list();
  Object.assign(list.find(c => c.id === cat.id), { locked: true, salt, check: await makeCheck(key) });
  await store.categories.save(list);
  keys.set(cat.id, key);
  toast("“" + cat.name + "” is locked");
}

export async function read(rec) {
  const key = keys.get(rec.category);
  if (!key) { toast("Unlock the category first"); return null; }
  try { return new Blob([await decryptBytes(key, rec.iv, await rec.blob.arrayBuffer())], { type: rec.type }); } catch { toast("Could not decrypt this file"); return null; }
}

// Add into a locked, open category: encrypted before it is stored.
export async function addEncrypted(catId, list) {
  const key = keys.get(catId);
  const recs = await store.addFiles(list, { category: catId });
  for (let i = 0; i < recs.length; i++) {
    const enc = await encryptBytes(key, await recs[i].blob.arrayBuffer());
    Object.assign(recs[i], { blob: new Blob([enc.data]), iv: enc.iv, locked: true });
    await store.files.put(recs[i]);
  }
  return recs;
}
