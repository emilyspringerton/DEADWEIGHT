// Run: PLAYWRIGHT_BROWSERS_PATH=<dir> node test/audio_clock_test.mjs <path-to-this-web-dir>   (needs: npm i playwright-core; npx playwright-core install chromium)
// Drives the REAL dist/fxWasm.js in headless Chromium against a controllable fake AudioContext and
// checks the audio stream never drifts audibly ahead of the hardware clock.
import { chromium } from 'playwright-core';
import { spawn } from 'node:child_process';

const ROOT = process.argv[2];
const srv = spawn('python3', ['-m', 'http.server', '18123', '--bind', '127.0.0.1'], { cwd: ROOT, stdio: 'ignore' });
await new Promise((r) => setTimeout(r, 800));
const browser = await chromium.launch();
const page = await browser.newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });

await page.addInitScript(() => {
  window.__sources = [];
  window.AudioContext = class {
    constructor() { this.currentTime = 0; this.state = 'running'; this.destination = {}; this.frozen = false; window.__ac = this; }
    createBuffer(ch, len) { return { getChannelData() { return new Float32Array(len); } }; }
    createBufferSource() {
      const s = { connect() {}, disconnect() {}, stop() { s.stopped = true; }, onended: null, start(t) { s.at = t; } };
      window.__sources.push(s); return s;
    }
    resume() { if (!this.noResume) this.state = 'running'; return Promise.resolve(); }
  };
});
await page.goto('http://127.0.0.1:18123/index.html');

const result = await page.evaluate(async () => {
  const m = await import('/dist/fxWasm.js');
  const canvas = document.createElement('canvas'); canvas.width = 480; canvas.height = 300;
  await m.initFxWasm(canvas);
  m.resetAudioClock();
  const reveal = { you: -1, opp: -1, have: false, round: 0, dmgYou: 0, dmgOpp: 0 };
  const ac = window.__ac;
  // "live" = sources that have not stopped and are still scheduled in the future (what the user would hear late)
  const backlog = () => window.__sources.filter((s) => !s.stopped && s.at > ac.currentTime + 0.001).length;
  const out = { scenarios: {} };
  let maxLead = -9;
  const step = (dt, advanceClock = true) => {
    if (advanceClock && !ac.frozen) ac.currentTime += dt / 1000;
    m.tick(dt, canvas, reveal);
    const d = m.audioDiagnostics();
    maxLead = Math.max(maxLead, d.leadSeconds);
  };

  // A: 5 s of normal 60 fps
  for (let i = 0; i < 300; i++) step(1000 / 60);
  out.scenarios.normal = { maxLead, resyncs: m.audioDiagnostics().resyncs, backlog: backlog() };

  // B: hardware clock stalls (state stays 'running', currentTime frozen) while rAF keeps ticking for 8 s
  maxLead = -9; ac.frozen = true;
  for (let i = 0; i < 480; i++) step(1000 / 60);
  const during = { maxLead, resyncs: m.audioDiagnostics().resyncs, backlog: backlog() };
  ac.frozen = false;
  for (let i = 0; i < 30; i++) step(1000 / 60);
  out.scenarios.clockStall = { during, afterRecovery: m.audioDiagnostics(), backlogAfter: backlog() };

  // C: context suspended (and refuses to resume) for 6 s: nothing may be queued
  const before = window.__sources.length;
  ac.state = 'suspended'; ac.noResume = true;
  for (let i = 0; i < 360; i++) step(1000 / 60);
  out.scenarios.suspended = { queuedWhileSuspended: window.__sources.length - before };
  ac.noResume = false; ac.state = 'running';
  for (let i = 0; i < 30; i++) step(1000 / 60);
  out.scenarios.resumed = m.audioDiagnostics();

  // D: a 5 s main-thread stall (one giant dt): only a short tail may be scheduled
  const b4 = window.__sources.length;
  step(5000);
  out.scenarios.longFrame = { scheduledSources: window.__sources.length - b4, lead: m.audioDiagnostics().leadSeconds };

  // E: after everything, normal play still has a sane lead
  maxLead = -9;
  for (let i = 0; i < 120; i++) step(1000 / 60);
  out.scenarios.recovered = { maxLead, ...m.audioDiagnostics() };
  return out;
});

console.log(JSON.stringify(result, null, 2));
const s = result.scenarios;
let fail = 0;
const ok = (c, msg) => { console.log((c ? 'PASS: ' : 'FAIL: ') + msg); if (!c) fail++; };
ok(s.normal.maxLead < 0.2 && s.normal.resyncs === 0, 'normal play keeps a small lead and never re-anchors');
ok(s.clockStall.during.maxLead < 0.25, `a frozen hardware clock never lets the stream run away (max lead ${s.clockStall.during.maxLead.toFixed(3)}s)`);
ok(s.clockStall.during.resyncs > 0, 'the stall is detected and re-anchored');
ok(s.clockStall.backlogAfter < 20, `queued backlog is flushed, not left to play late (${s.clockStall.backlogAfter} future sources)`);
ok(s.clockStall.afterRecovery.leadSeconds < 0.2, 'lead is back in range once the clock resumes');
ok(s.suspended.queuedWhileSuspended === 0, 'nothing is queued while the context is suspended');
ok(s.longFrame.scheduledSources <= 3, `a long frame schedules only its short tail (${s.longFrame.scheduledSources} chunks)`);
ok(s.recovered.maxLead < 0.2 && s.recovered.state === 'running', 'normal playback afterwards keeps a sane lead');
ok(errors.length === 0, 'no page errors: ' + errors.join(' | '));
await browser.close(); srv.kill();
process.exit(fail ? 1 : 0);
