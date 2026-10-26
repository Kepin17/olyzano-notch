// Shared state and a tiny event bus for the notch's pages.
import { prefs } from "./store.js";

export const state = {
  tab: "files",
  prev: "files",
  query: "",
  category: "",          // "" = all, "__bin" = Recently removed
  selection: new Set(),
  sort: prefs.get("sort", "date"),
  pinned: prefs.get("pinned", true),
  open: false,
};

const handlers = {};
export const on = (ev, fn) => (handlers[ev] = handlers[ev] || []).push(fn);
export const emit = (ev, ...a) => (handlers[ev] || []).forEach(fn => fn(...a));

export const TABS = [
  ["files", "Files", "folder"],
  ["tools", "Tools", "gear"],
  ["timer", "Timer", "timer"],
  ["color", "Color", "palette"],
  ["share", "Share", "share"],
  ["music", "Music", "music"],
];
