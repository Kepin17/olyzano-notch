// Timer, Pomodoro and stopwatch, plus the stretch reminder. Time comes from timestamps, so a background tab stays right.
import { h, $, mount, clock, toast } from "./util.js";
import { icon } from "./icons.js";
import { prefs } from "./store.js";

let T = prefs.get("timer", null);            // {kind, label, endsAt, total, paused, left, round, phase, startedAt}
let tick;
const save = () => prefs.set("timer", T);
const now = () => Date.now();
const leftMs = () => !T ? 0 : T.kind === "stopwatch" ? (T.paused ? T.left : now() - T.startedAt) : T.paused ? T.left : Math.max(0, T.endsAt - now());

function beep() {
  try {
    const a = new AudioContext(), o = a.createOscillator(), g = a.createGain();
    o.connect(g); g.connect(a.destination); o.type = "sine"; o.frequency.value = 880; g.gain.setValueAtTime(.001, a.currentTime);
    g.gain.exponentialRampToValueAtTime(.3, a.currentTime + .02); g.gain.exponentialRampToValueAtTime(.001, a.currentTime + .6);
    o.start(); o.stop(a.currentTime + .65);
  } catch { /* no audio */ }
}
function notify(title, body) {
  beep();
  toast(title + (body ? " · " + body : ""));
  if ("Notification" in window && Notification.permission === "granted") try { new Notification(title, { body, icon: "icons/notch.png" }); } catch { /* ok */ }
}

function start(kind, seconds, label = "") {
  const f = prefs.get("focus", 25), b = prefs.get("break", 5);
  T = kind === "stopwatch" ? { kind, startedAt: now(), paused: false, label: "Stopwatch" }
    : kind === "pomodoro" ? { kind, phase: "Focus", round: 1, label: "Focus", total: f * 60000, endsAt: now() + f * 60000, paused: false }
    : { kind, label: label || "Timer", total: seconds * 1000, endsAt: now() + seconds * 1000, paused: false };
  save(); run();
}
const stop = () => { T = null; save(); refreshPill(); };
const pause = () => { if (!T || T.paused) return; T.left = leftMs(); T.paused = true; save(); };
const resume = () => { if (!T || !T.paused) return; if (T.kind === "stopwatch") T.startedAt = now() - T.left; else T.endsAt = now() + T.left; T.paused = false; save(); };

function run() { clearInterval(tick); tick = setInterval(step, 250); step(); }
function step() {
  if (!T) { clearInterval(tick); return; }
  if (!T.paused && T.kind !== "stopwatch" && now() >= T.endsAt) {
    if (T.kind === "pomodoro") {
      const f = prefs.get("focus", 25), b = prefs.get("break", 5);
      if (T.phase === "Focus") { notify("Time for a break", b + " minutes"); T = { ...T, phase: "Break", label: "Break", total: b * 60000, endsAt: now() + b * 60000 }; }
      else { notify("Back to focus", "Round " + (T.round + 1)); T = { ...T, phase: "Focus", label: "Focus", round: T.round + 1, total: f * 60000, endsAt: now() + f * 60000 }; }
      save();
    } else { notify(T.label + " is done"); T = null; save(); }
  }
  refreshPill();
  const ring = $("#ringArc"), time = $("#ringTime");
  if (ring && T) { const frac = T.kind === "stopwatch" ? (leftMs() % 60000) / 60000 : Math.max(0, Math.min(1, leftMs() / T.total)); ring.style.strokeDashoffset = String(2 * Math.PI * 86 * (1 - frac)); }
  if (time && T) time.firstChild.textContent = clock(leftMs());
  else if (!T && $("#ringArc")) document.dispatchEvent(new CustomEvent("timer-ended"));
}
function refreshPill() {
  const el = $("#pillText");
  if (!el) return;
  el.textContent = T ? (T.kind === "stopwatch" ? "⏱ " : T.phase === "Break" ? "🧘 " : "⏱ ") + clock(leftMs()) : new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}
setInterval(refreshPill, 10000); refreshPill();
if (T) run();
document.addEventListener("timer-ended", () => $("#page")?.dataset.view === "timer" && render({ page: $("#page"), refresh: () => render({ page: $("#page"), refresh: () => {} }) }));

export async function render(ctx) {
  ctx.page.dataset.view = "timer";
  const mode = prefs.get("timerMode", "timer");
  const C = 2 * Math.PI * 86;
  if (T) {
    const dash = T.kind === "stopwatch" ? 0 : C * (1 - Math.max(0, Math.min(1, leftMs() / T.total)));
    mount(ctx.page,
      h("div", { class: "row", style: { justifyContent: "center", marginBottom: "4px" } }, h("span", { class: "chip", "aria-pressed": "true" }, T.label + (T.kind === "pomodoro" ? " · round " + T.round : ""))),
      h("div", { class: "ring" },
        h("div", { style: { width: "100%", height: "100%" }, html: `<svg viewBox="0 0 200 200"><circle cx="100" cy="100" r="86" fill="none" stroke="var(--surfacePressed)" stroke-width="10"/><circle id="ringArc" cx="100" cy="100" r="86" fill="none" stroke="${T.phase === "Break" ? "var(--success)" : "var(--accent)"}" stroke-width="10" stroke-linecap="round" stroke-dasharray="${C}" stroke-dashoffset="${dash}" style="transition:stroke-dashoffset .25s linear"/></svg>` }),
        h("div", { class: "time", id: "ringTime" }, clock(leftMs()), h("small", {}, T.paused ? "paused" : T.kind === "stopwatch" ? "counting" : "left"))),
      h("div", { class: "row", style: { justifyContent: "center", gap: "14px" } },
        h("button", { class: "round", "aria-label": "Stop", html: icon("stop", 20), onclick: () => { stop(); ctx.refresh(); } }),
        h("button", { class: "round primary", "aria-label": T.paused ? "Resume" : "Pause", html: icon(T.paused ? "play" : "pause", 24), onclick: () => { T.paused ? resume() : pause(); ctx.refresh(); } }),
        T.kind !== "stopwatch" ? h("button", { class: "round", "aria-label": "Add a minute", title: "+1 min", html: icon("plus", 20), onclick: () => { if (T.paused) T.left += 60000; else T.endsAt += 60000; T.total += 60000; save(); ctx.refresh(); } }) : null));
    return;
  }
  const mins = h("input", { class: "field", type: "number", min: 1, max: 600, value: prefs.get("custom", 15), style: { width: "90px" }, onchange: e => prefs.set("custom", +e.target.value || 15) });
  const label = h("input", { class: "field", placeholder: "Name (optional), e.g. Tea", style: { flex: "1 1 160px" } });
  mount(ctx.page,
    h("div", { class: "seg" }, ...[["timer", "Timer"], ["pomodoro", "Pomodoro"], ["stopwatch", "Stopwatch"]].map(([v, t]) => h("button", { "aria-pressed": String(mode === v), onclick: () => { prefs.set("timerMode", v); ctx.refresh(); } }, t))),
    mode === "timer" ? [h("h3", {}, "One tap"), h("div", { class: "presets" }, ...[1, 3, 5, 10, 15, 30, 45, 60].map(m => h("button", { class: "preset", onclick: () => { start("timer", m * 60); ctx.refresh(); } }, m, h("small", {}, "min")))),
      h("h3", {}, "Or your own"), h("div", { class: "row" }, mins, label, h("button", { class: "tbtn primary", onclick: () => { start("timer", Math.max(1, +mins.value) * 60, label.value); ctx.refresh(); } }, h("span", { html: icon("play", 13) }), "Start"))] : null,
    mode === "pomodoro" ? [h("h3", {}, "Focus"), h("div", { class: "seg" }, ...[25, 50].map(m => h("button", { "aria-pressed": String(prefs.get("focus", 25) === m), onclick: () => { prefs.set("focus", m); ctx.refresh(); } }, m + " min"))),
      h("h3", {}, "Break"), h("div", { class: "seg" }, ...[5, 10, 15].map(m => h("button", { "aria-pressed": String(prefs.get("break", 5) === m), onclick: () => { prefs.set("break", m); ctx.refresh(); } }, m + " min"))),
      h("p", { class: "muted" }, "Round after round until you stop it."), h("button", { class: "tbtn primary", onclick: () => { start("pomodoro"); ctx.refresh(); } }, h("span", { html: icon("play", 13) }), "Start")] : null,
    mode === "stopwatch" ? [h("p", { class: "muted" }, "Counts up until you stop it."), h("button", { class: "tbtn primary", onclick: () => { start("stopwatch"); ctx.refresh(); } }, h("span", { html: icon("play", 13) }), "Start")] : null,
    h("p", { class: "muted", style: { marginTop: "14px" } }, "The time keeps running in the closed notch, and rings (with a notification if you allowed them in Settings) even when this tab is in the background."));
}

// Stretch reminder: counts the time you are active in this tab; asks for a break after a while.
let active = 0, lastInput = 0, stretchTimer;
["pointermove", "keydown", "pointerdown", "wheel"].forEach(ev => addEventListener(ev, () => { lastInput = Date.now(); }, { passive: true }));
export function syncStretch() {
  clearInterval(stretchTimer);
  if (!prefs.get("stretch", false)) return;
  stretchTimer = setInterval(() => {
    if (document.hidden || Date.now() - lastInput > 60000) { if (Date.now() - lastInput > 5 * 60000) active = 0; return; }
    active += 10000;
    if (active >= 50 * 60000) { active = 0; notify("Time to stretch", "You have worked for 50 minutes. A short break helps."); }
  }, 10000);
}
