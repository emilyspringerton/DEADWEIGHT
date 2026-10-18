// Real-browser test of the CONSTRUCTED deck builder and the whole flow behind it: build a 23-card deck out of the full card pool with
// the 1X / 2X / 3X buttons, filter and sort the pool, save and load named decks, queue with it, play a match against a bot through
// the real ws<->tcp bridge and a real dw_server, then come back with "Edit deck" and queue again.
//   scripts/build-web.sh /tmp/dw-web                      the tree WOTAN serves at /DEADWEIGHT/ (needs clang + wasm-ld)
//   scripts/build.sh                                      build/dw_server and build/dw_bot
//   (cd web/bridge && npm ci)                             the bridge's one dependency (ws)
//   NODE_PATH=<dir with node_modules/playwright> PLAYWRIGHT_BROWSERS_PATH=... DEADWEIGHT_SITE=/tmp/dw-web \
//     node web/test/constructed_builder_test.mjs [screenshot-dir]
// IDUNA is a small stub (guest register, tickets, a profile with a rating); everything else is the real thing: the compiled client,
// the wasm codec and rules, the bridge, dw_server (--no-auth) and three dw_bot --mode constructed bots.
import { createRequire } from 'node:module';
import http from 'node:http';
import net from 'node:net';
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, '..', '..');
const site = process.env.DEADWEIGHT_SITE;
if (!site || !fs.existsSync(path.join(site, 'dist', 'main.js'))) {
  console.error('Set DEADWEIGHT_SITE to the output of scripts/build-web.sh (it must contain dist/main.js).');
  process.exit(2);
}
for (const b of ['dw_server', 'dw_bot']) {
  if (!fs.existsSync(path.join(root, 'build', b))) { console.error(`build/${b} is missing: run scripts/build.sh first.`); process.exit(2); }
}
if (!fs.existsSync(path.join(here, '..', 'bridge', 'node_modules', 'ws'))) { console.error('Run `npm ci` in web/bridge first (the bridge needs ws).'); process.exit(2); }
const outdir = process.argv[2] || path.join(here, '..', '..', '..', 'constructed-shots');
fs.mkdirSync(outdir, { recursive: true });

let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------- the real server side: dw_server + bots + bridge ----------
const freePort = () => new Promise((res) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
const tcpPort = await freePort(), bridgePort = await freePort();
const procs = [];
const start = (cmd, args, name) => {
  const p = spawn(cmd, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  p.log = '';
  p.stdout.on('data', (d) => { p.log += d; });
  p.stderr.on('data', (d) => { p.log += d; });
  p.name = name;
  procs.push(p);
  return p;
};
const server = start(path.join(root, 'build', 'dw_server'), ['--port', String(tcpPort), '--no-auth'], 'dw_server');
await sleep(300);
for (const arch of ['ripper', 'wall', 'mirror']) {
  start(path.join(root, 'build', 'dw_bot'), ['--archetype', arch, '--name', `bot-${arch}-c`, '--mode', 'constructed', '--port', String(tcpPort), '--think-ms', '0', '--seed', String(7 + arch.length)], 'bot-' + arch);
}
start('node', [path.join(here, '..', 'bridge', 'ws-tcp-bridge.js'), '--ws-host', '127.0.0.1', '--ws-port', String(bridgePort), '--tcp-port', String(tcpPort)], 'bridge');
await sleep(600);

// ---------- the stub IDUNA + the static site, and /DEADWEIGHT/ws passed through to the real bridge ----------
let rating = 1432.6;
let profileReads = 0;
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.wasm': 'application/wasm', '.map': 'application/json' };
const web = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const p = decodeURIComponent(u.pathname);
  const json = (code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };
  const api = '/api/v1/games/deadweight';
  if (p.startsWith(api + '/')) {
    const sub = p.slice(api.length);
    const exp = Math.floor(Date.now() / 1000) + 3600;
    if (req.method === 'POST' && sub === '/guest-register') return json(201, { player_id: 'guest-1', guest_secret: 'sekrit', display_name: 'Runner-TEST', token: 'guest.token', expires_at: exp, tickets: 1 });
    let m = /^\/players\/([^/]+)\/tickets$/.exec(sub);
    if (m) return json(200, { player_id: m[1], tickets: 1, founder: false });
    m = /^\/players\/([^/]+)\/profile$/.exec(sub);
    if (m) { profileReads++; return json(200, { player_id: m[1], display_name: 'Runner-TEST', kind: 'human', rating, wins: 3, losses: 2, draws: 0, matches: 5, friend_count: 0 }); }
    return json(404, { error: 'not found' });
  }
  if (p.startsWith('/DEADWEIGHT/')) {
    let rel = p.slice('/DEADWEIGHT/'.length);
    if (rel === '') rel = 'index.html';
    const f = path.join(site, rel);
    if (!f.startsWith(site) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end('nf'); return; }
    res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' });
    fs.createReadStream(f).pipe(res); return;
  }
  res.writeHead(404); res.end('nf');
});
web.on('upgrade', (req, sock, head) => {
  if (!req.url.startsWith('/DEADWEIGHT/ws')) { sock.destroy(); return; }
  const up = net.connect(bridgePort, '127.0.0.1', () => {
    up.write(`GET / HTTP/1.1\r\n` + Object.entries(req.headers).map(([k, v]) => `${k}: ${v}`).join('\r\n') + '\r\n\r\n');
    if (head && head.length) up.write(head);
    sock.pipe(up); up.pipe(sock);
  });
  up.on('error', () => sock.destroy());
  sock.on('error', () => up.destroy());
});
await new Promise((r) => web.listen(0, '127.0.0.1', r));
const port = web.address().port;
// dw.test resolves to 127.0.0.1 but is NOT "localhost", so the client takes its production branches (same-origin IDUNA, /DEADWEIGHT/ws)
const browser = await chromium.launch({ args: ['--host-resolver-rules=MAP dw.test 127.0.0.1'] });
const base = `http://dw.test:${port}`;
const errors = [];
async function newPage(seedStorage) {
  const ctx = await browser.newContext({ viewport: { width: 1000, height: 1100 } });
  if (seedStorage) await ctx.addInitScript((kv) => { try { for (const k in kv) localStorage.setItem(k, kv[k]); } catch (e) { /* ignore */ } }, seedStorage);
  await ctx.route('**/fonts.googleapis.com/**', (r) => r.abort());
  await ctx.route('**/fonts.gstatic.com/**', (r) => r.abort());
  const page = await ctx.newPage();
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message + ' @ ' + String(e.stack || '').split('\n').slice(1, 3).join(' <- ').replace(/http:\/\/dw\.test:\d+/g, '')));
  page.on('console', (m) => { if (m.type() === 'error' && !/Failed to load resource|fonts\.g|ERR_/.test(m.text())) errors.push('console: ' + m.text()); });
  return page;
}
const visible = (page, sel) => page.locator(sel).isVisible();
const text = async (page, sel) => (await page.locator(sel).textContent()).replace(/\s+/g, ' ').trim();
const shot = (page, name) => page.screenshot({ path: path.join(outdir, name) });
const TOTAL = JSON.parse(fs.readFileSync(path.join(site, 'src', 'generated', 'cards.json'), 'utf8')).cards.length;

// pick `m`X of the n-th visible tile of the pool
const tile = (page, id) => page.locator(`#cons-pool .cons-card[data-id="${id}"]`);
async function choose(page, id, m) { await tile(page, id).locator(`button[data-m="${m}"]`).click(); }
const left = async (page) => text(page, '#cons-left');
const deckLine = async (page) => text(page, '#cons-size');

try {
  const page = await newPage();
  await page.goto(base + '/DEADWEIGHT/');
  await page.click('#start-btn');
  await page.waitForFunction(() => document.getElementById('status').textContent === 'ready', null, { timeout: 15000 });
  ok(await visible(page, '#cons-btn') && !(await page.locator('#cons-btn').isDisabled()), 'in game: the "Constructed" button sits beside Queue and Draft, enabled once the server has said WELCOME');
  ok(/Constructed/.test(await text(page, '#cons-btn')) && /free/i.test(await text(page, '#cons-btn')), 'it says what it is (build a deck, free)');
  ok(!(await visible(page, '#constructed')), 'the builder is not on screen until asked for');

  await page.click('#cons-btn');
  await page.waitForSelector('#constructed', { state: 'visible' });
  ok(!(await visible(page, '#queue-btn')) && !(await visible(page, '#draft-btn')) && !(await visible(page, '#cons-btn')), 'the builder replaces the game buttons');
  const tiles = await page.locator('#cons-pool .cons-card').count();
  ok(tiles === TOTAL, `the pool shows every card of the catalog (${tiles} of ${TOTAL}) -- no packs, everyone has all of them`);
  ok((await text(page, '#cons-count')) === `SHOWING ${TOTAL} OF ${TOTAL}`, 'the count line says so');
  ok((await text(page, '#cons-left')) === 'LEFT 1X: 10 2X: 5 3X: 1' && (await deckLine(page)) === 'DECK 0/23', 'an empty deck: ten 1X, five 2X, one 3X left; 0 of 23');
  ok(await page.locator('#cons-queue').isDisabled(), 'QUEUE CONSTRUCTED is locked until the deck is complete');
  ok(/PICK 16 MORE CARDS/.test(await text(page, '#cons-need')), 'and the panel says what is missing: ' + (await text(page, '#cons-need')));
  await page.waitForFunction(() => /YOUR RATING: 1433/.test(document.getElementById('cons-rating').textContent), null, { timeout: 5000 });
  ok(true, 'the player\'s ladder rating is shown (the server pairs you with the bot nearest it): ' + (await text(page, '#cons-rating')));
  await shot(page, '01-empty-builder.png');

  // ----- 1X / 2X / 3X -----
  await choose(page, 5, 1);
  ok((await left(page)) === 'LEFT 1X: 9 2X: 5 3X: 1' && (await deckLine(page)) === 'DECK 1/23', 'choosing 1X of a card updates the buckets and the size');
  ok(await tile(page, 5).evaluate((e) => e.classList.contains('in')), 'the card\'s tile is marked as in the deck');
  ok((await page.locator('#cons-list li[data-id="5"]').count()) === 1 && /1X/.test(await text(page, '#cons-list li[data-id="5"]')), 'it appears in the deck list beside the pool, under 1X');
  await choose(page, 5, 3);
  ok((await left(page)) === 'LEFT 1X: 10 2X: 5 3X: 0' && (await deckLine(page)) === 'DECK 3/23', 'choosing 3X moves it up: the 1X slot is freed, the 3X slot taken, the size is 3');
  ok(await tile(page, 9).locator('button[data-m="3"]').isDisabled(), 'with the 3X slot taken, every other card\'s 3X button is greyed out');
  await choose(page, 5, 3);
  ok((await deckLine(page)) === 'DECK 0/23' && (await page.locator('#cons-list li[data-id="5"]').count()) === 0, 'choosing the same number again takes the card back out');
  await choose(page, 6, 2);
  await page.locator('#cons-list li[data-id="6"] button[data-remove]').click();
  ok((await deckLine(page)) === 'DECK 0/23', 'the x in the deck list takes a card out');

  // ----- filters and sort -----
  await page.fill('#cons-q', 'zzzzqqqq');
  ok((await page.locator('#cons-pool .cons-card').count()) === 0 && /NO CARDS MATCH/.test(await text(page, '#cons-pool')), 'a search with no hits says so instead of showing an empty box');
  await page.fill('#cons-q', '');
  await page.locator('#cons-kinds button[data-kind="0"]').click();
  const off = await page.locator('#cons-pool .cons-card').count();
  ok(off > 0 && off < TOTAL && (await page.locator('#cons-pool .cons-card:not([data-kind="0"])').count()) === 0, `the OFFENSE filter shows only offense cards (${off})`);
  ok((await page.locator('#cons-kinds button[aria-pressed="true"]').count()) === 1 && (await text(page, '#cons-kinds button[aria-pressed="true"]')) === 'OFFENSE', 'and the pressed button says which');
  await page.locator('#cons-cost').selectOption({ index: 2 });
  const offCost = await page.locator('#cons-pool .cons-card').count();
  ok(offCost > 0 && offCost < off, `a cost filter narrows it further (${offCost})`);
  await page.click('#cons-reset');
  ok((await page.locator('#cons-pool .cons-card').count()) === TOTAL, 'RESET FILTERS brings the whole pool back');
  await page.fill('#cons-q', 'swap');
  const swaps = await page.locator('#cons-pool .cons-card').count();
  ok(swaps >= 1 && swaps < TOTAL, `searching the rules text finds cards (${swaps} mention "swap")`);
  await page.fill('#cons-q', '');
  await page.locator('#cons-sort').selectOption('name');
  const names = await page.locator('#cons-pool .cons-card .card-header').allTextContents();
  const sorted = [...names].sort((a, b) => a.toLowerCase().localeCompare(b.toLowerCase()));
  ok(names.length === TOTAL && JSON.stringify(names.map((n) => n.toLowerCase())) === JSON.stringify(sorted.map((n) => n.toLowerCase())), 'sorting by name puts the pool in name order');
  await page.locator('#cons-sort').selectOption('cost');

  // ----- build a whole deck by hand -----
  const ids = [3, 8, 12, 15, 20, 25, 30, 35, 40, 44, 47, 50, 55, 60, 70, 80].filter((i) => i < TOTAL);
  ok(ids.length === 16, 'the catalog is big enough for the test deck');
  await choose(page, ids[15], 3);
  for (let i = 10; i < 15; i++) await choose(page, ids[i], 2);
  for (let i = 0; i < 10; i++) await choose(page, ids[i], 1);
  ok((await deckLine(page)) === 'DECK 23/23' && (await left(page)) === 'LEFT 1X: 0 2X: 0 3X: 0', 'ten singles, five pairs and a triple make DECK 23/23');
  ok(await tile(page, 2).locator('button[data-m="1"]').isDisabled(), 'with the buckets full the unchosen cards\' buttons are all greyed out');
  ok((await text(page, '#cons-need')) === 'DECK READY' && !(await page.locator('#cons-queue').isDisabled()), 'DECK READY, and QUEUE CONSTRUCTED unlocks');
  await page.locator('#cons-indeck').click();
  ok((await page.locator('#cons-pool .cons-card').count()) === 16, 'IN DECK shows exactly the sixteen chosen cards');
  await shot(page, '02-full-deck.png');
  await page.locator('#cons-indeck').click();

  // ----- save, clear, load, delete -----
  await page.fill('#cons-name', 'My Rush');
  await page.click('#cons-save');
  ok(/SAVED "My Rush"/.test(await text(page, '#cons-msg')) && (await page.locator('#cons-saved option').count()) === 2, 'SAVE names the deck and lists it');
  await page.click('#cons-clear');
  ok((await deckLine(page)) === 'DECK 0/23', 'CLEAR DECK empties the list');
  await page.locator('#cons-saved').selectOption({ label: 'MY RUSH' }).catch(async () => { await page.locator('#cons-saved').selectOption({ index: 1 }); });
  ok((await deckLine(page)) === 'DECK 23/23', 'picking the saved deck loads it back, complete');
  await page.reload();
  await page.waitForSelector('#start-btn');
  ok(true, 'reloaded the page');
  await page.click('#start-btn');
  await page.waitForFunction(() => document.getElementById('status').textContent === 'ready', null, { timeout: 15000 });
  await page.click('#cons-btn');
  await page.waitForSelector('#constructed', { state: 'visible' });
  ok((await deckLine(page)) === 'DECK 23/23' && (await page.locator('#cons-saved option').count()) === 2, 'after a reload the deck being built and the saved deck are still there (localStorage)');
  await page.locator('#cons-saved').selectOption({ index: 1 });
  await page.click('#cons-del');
  ok(/DELETED/.test(await text(page, '#cons-msg')) && (await page.locator('#cons-saved option').count()) === 1, 'DELETE removes a saved deck');
  await page.click('#cons-clear');
  await page.click('#cons-fill');
  ok((await deckLine(page)) === 'DECK 23/23' && !(await page.locator('#cons-queue').isDisabled()), 'FILL THE REST AT RANDOM completes an empty deck into a legal one');
  await page.click('#cons-clear');
  await choose(page, 3, 3); await choose(page, 4, 2);
  await page.click('#cons-fill');
  ok((await deckLine(page)) === 'DECK 23/23' && (await page.locator('#cons-list li[data-id="3"]').textContent()).includes('3X') && (await page.locator('#cons-list li[data-id="4"]').textContent()).includes('2X'), 'random fill keeps the cards that were already chosen');
  await page.click('#cons-clear');
  // the hand-built deck again, for the match
  await choose(page, ids[15], 3);
  for (let i = 10; i < 15; i++) await choose(page, ids[i], 2);
  for (let i = 0; i < 10; i++) await choose(page, ids[i], 1);
  const deckIds = new Set(ids);

  // ----- back -----
  await page.click('#cons-back');
  ok(!(await visible(page, '#constructed')) && await visible(page, '#queue-btn') && await visible(page, '#cons-btn'), 'BACK returns to the game buttons');
  await page.click('#cons-btn');
  await page.waitForSelector('#constructed', { state: 'visible' });
  ok((await deckLine(page)) === 'DECK 23/23', 'and the deck is still there when the builder is opened again');

  // ----- queue, play a match against a bot -----
  const handCards = []; // what was dealt in round 1 of each match
  await page.click('#cons-queue');
  await page.waitForFunction(() => /queued|in_match|match over/.test(document.getElementById('status').textContent), null, { timeout: 15000 });
  ok(!(await visible(page, '#constructed')), 'QUEUE CONSTRUCTED leaves the builder');
  await page.waitForSelector('#match', { state: 'visible', timeout: 20000 });
  ok(/\(bot\)/.test(await text(page, '#opp-name')), 'matched against a bot: ' + (await text(page, '#opp-name')));
  async function playOut() {
    let first = true;
    for (let i = 0; i < 60; i++) {
      if (await visible(page, '#requeue')) break;
      // the hand is on screen at the start of every round
      if (first && (await page.locator('#hand .card-header').count()) >= 1) {
        await page.waitForFunction(() => document.querySelectorAll('#hand button').length > 0, null, { timeout: 5000 }).catch(() => {});
        handCards.push(await page.locator('#hand .card-header').allTextContents());
        first = false;
      }
      // clicking Pass only SELECTS it (as in the native client); Lock in is what sends the play
      const pass = page.locator('#pass-btn'), lock = page.locator('#lockin-btn');
      if (await pass.isVisible() && !(await pass.isDisabled())) await pass.click().catch(() => {});
      if (await lock.isVisible() && !(await lock.isDisabled())) await lock.click().catch(() => {});
      await sleep(250);
    }
  }
  await playOut();
  await page.waitForSelector('#requeue', { state: 'visible', timeout: 40000 });
  ok(/match over/.test(await text(page, '#status')), 'the match was played to the end: ' + (await text(page, '#status')));
  const opening = handCards[0] || [];
  const catalog = JSON.parse(fs.readFileSync(path.join(site, 'src', 'generated', 'cards.json'), 'utf8')).cards;
  const deckNames = new Set([...deckIds].map((i) => catalog[i].name.toLowerCase()));
  const openNames = opening.map((s) => s.replace(/\s+/g, ' ').trim().toLowerCase()).filter((s) => s && s !== '—');
  ok(openNames.length >= 3 && openNames.every((n) => deckNames.has(n)), `the opening hand came out of the deck that was built (${openNames.join(', ')})`);
  ok(await visible(page, '#cons-edit') && /Edit deck/i.test(await text(page, '#cons-edit')), 'after the match: "Edit deck" is offered next to "Queue again"');
  await shot(page, '03-match-over.png');

  // ----- queue again with the same deck -----
  await page.click('#requeue');
  await page.waitForFunction(() => /queued|in_match/.test(document.getElementById('status').textContent), null, { timeout: 15000 });
  await page.waitForSelector('#requeue', { state: 'hidden', timeout: 5000 });
  await playOut();
  await page.waitForSelector('#requeue', { state: 'visible', timeout: 40000 });
  ok(/match over/.test(await text(page, '#status')), '"Queue again" plays the same deck again, no resubmitting');

  // ----- edit deck: comes back with the deck that was played -----
  await page.click('#cons-edit');
  await page.waitForSelector('#constructed', { state: 'visible' });
  ok((await deckLine(page)) === 'DECK 23/23', '"Edit deck" opens the builder with the deck that was just played');
  await page.locator('#cons-list li button[data-remove]').first().click();
  ok((await deckLine(page)) !== 'DECK 23/23' && await page.locator('#cons-queue').isDisabled(), 'taking a card out makes it incomplete again and locks QUEUE');
  await page.click('#cons-fill');
  ok(!(await page.locator('#cons-queue').isDisabled()), 'fill it back up');
  await page.click('#cons-queue');
  await page.waitForSelector('#match', { state: 'visible', timeout: 20000 });
  await playOut();
  await page.waitForSelector('#requeue', { state: 'visible', timeout: 40000 });
  ok(/match over/.test(await text(page, '#status')), 'the edited deck plays a match too');

  // ----- going back to the random queue from a constructed connection -----
  await page.click('#queue-btn');
  await page.waitForFunction(() => /queued|in_match|ready/.test(document.getElementById('status').textContent), null, { timeout: 15000 });
  ok(true, 'the random "Queue" button still works after constructed play: ' + (await text(page, '#status')));
  await page.waitForSelector('#match', { state: 'visible', timeout: 20000 });

  await page.context().close();

  // ----- a narrow phone -----
  const phone = await newPage();
  await phone.setViewportSize({ width: 390, height: 800 });
  await phone.goto(base + '/DEADWEIGHT/');
  await phone.click('#start-btn');
  await phone.waitForFunction(() => document.getElementById('status').textContent === 'ready', null, { timeout: 15000 });
  await phone.click('#cons-btn');
  await phone.waitForSelector('#constructed', { state: 'visible' });
  const overflow = await phone.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  ok(overflow <= 1, `on a 390px phone the builder does not scroll sideways (overflow ${overflow}px)`);
  await shot(phone, '04-phone.png');
  await phone.context().close();

  ok(errors.length === 0, 'no page errors or console errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
  const serverLog = procs.find((p) => p.name === 'dw_server').log;
  ok(!/ERROR|Sanitizer|abort/i.test(serverLog), 'dw_server logged no errors');
} catch (e) {
  fail++;
  console.log('FAIL: test aborted: ' + (e && e.stack || e));
  if (errors.length) console.log('page/console errors so far: ' + errors.join(' | '));
} finally {
  await browser.close();
  web.close();
  for (const p of procs) p.kill('SIGTERM');
}
console.log(`\n${pass} passed, ${fail} failed (screenshots in ${outdir})`);
process.exit(fail ? 1 : 0);
