// Locked categories: AES-GCM with a key derived from the password (PBKDF2). The password is never stored.
const enc = new TextEncoder(), dec = new TextDecoder();
const b64 = buf => btoa(String.fromCharCode(...new Uint8Array(buf)));
const unb64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));

export const newSalt = () => b64(crypto.getRandomValues(new Uint8Array(16)));

export async function deriveKey(password, saltB64) {
  const base = await crypto.subtle.importKey("raw", enc.encode(password), "PBKDF2", false, ["deriveKey"]);
  return crypto.subtle.deriveKey({ name: "PBKDF2", salt: unb64(saltB64), iterations: 250000, hash: "SHA-256" }, base,
    { name: "AES-GCM", length: 256 }, false, ["encrypt", "decrypt"]);
}
export async function encryptBytes(key, bytes) {
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const data = await crypto.subtle.encrypt({ name: "AES-GCM", iv }, key, bytes);
  return { iv: b64(iv), data };
}
export async function decryptBytes(key, iv, data) { return crypto.subtle.decrypt({ name: "AES-GCM", iv: unb64(iv) }, key, data); }

// A short known text, encrypted: opening it tells whether a password is right.
export async function makeCheck(key) { const r = await encryptBytes(key, enc.encode("notch")); return { iv: r.iv, data: b64(r.data) }; }
export async function checkKey(key, check) {
  try { return dec.decode(await decryptBytes(key, check.iv, unb64(check.data))) === "notch"; } catch { return false; }
}
