// Real-browser test of the DEADWEIGHT web client's founder-mode affordances: the "Redeem a code" link to WOTAN's page, the
// gold FOUNDER badge (the native client's own mark), the hint for guests, and the ticket refresh when the tab is looked at
// again (the link opens WOTAN in a NEW tab, so the answer arrives while this one is in the background).
//   scripts/build-web.sh /tmp/dw-web                      builds the tree WOTAN serves at /DEADWEIGHT/ (needs clang + wasm-ld)
//   NODE_PATH=<dir with node_modules/playwright> PLAYWRIGHT_BROWSERS_PATH=... DEADWEIGHT_SITE=/tmp/dw-web \
//     node web/test/redeem_link_test.mjs [outdir]
// IDUNA is a small stub here (only the endpoints this page reads, with a balance the test changes under it); the real IDUNA
// path -- a code redeemed on WOTAN landing on the player this client signs in as, 9999 tickets, the founder flag -- is
// exercised end to end by WOTAN's scripts/redeem-browser-test.mjs and IDUNA's account_redeem_test.go.
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const here = path.dirname(fileURLToPath(import.meta.url));
const site = process.env.DEADWEIGHT_SITE;
if (!site || !fs.existsSync(path.join(site, 'dist', 'main.js'))) {
  console.error('Set DEADWEIGHT_SITE to the output of scripts/build-web.sh (it must contain dist/main.js).');
  process.exit(2);
}
const outdir = process.argv[2] || path.join(here, '..', '..', '..', 'redeem-link-shots');
fs.mkdirSync(outdir, { recursive: true });

let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

// ---------- the stub IDUNA + the static site ----------
let tickets = { tickets: 1, founder: false };
let ticketReads = 0;
let staleHtml = false; // serve an index.html from before this feature, next to the fresh main.js (what a cached page can do)
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.wasm': 'application/wasm', '.map': 'application/json' };
const server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const p = decodeURIComponent(u.pathname);
  const json = (code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };
  const api = '/api/v1/games/deadweight';
  if (p.startsWith(api + '/')) {
    const sub = p.slice(api.length);
    const exp = Math.floor(Date.now() / 1000) + 3600;
    if (req.method === 'POST' && sub === '/guest-register') return json(201, { player_id: 'guest-1', guest_secret: 'sekrit', display_name: 'Runner-TEST', token: 'guest.token', expires_at: exp, tickets: tickets.tickets });
    if (req.method === 'POST' && sub === '/sso-exchange') return json(200, { player_id: 'sso-1', display_name: 'Gary', token: 'sso.token', expires_at: exp, tickets: tickets.tickets, account_state: 'base' });
    const m = /^\/players\/([^/]+)\/tickets$/.exec(sub);
    if (m) { ticketReads++; return json(200, { player_id: m[1], tickets: tickets.tickets, founder: tickets.founder }); }
    return json(404, { error: 'not found' });
  }
  if (p === '/redeem/') { res.writeHead(200, { 'content-type': 'text/html' }); res.end('<!doctype html><title>Redeem a Code</title><h1>Redeem a Code (stub)</h1>'); return; }
  if (p.startsWith('/DEADWEIGHT/')) {
    let rel = p.slice('/DEADWEIGHT/'.length);
    if (rel === '') rel = 'index.html';
    const f = path.join(site, rel);
    if (!f.startsWith(site) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end('nf'); return; }
    if (staleHtml && rel === 'index.html') {
      const html = fs.readFileSync(f, 'utf8')
        .replace(/<span id="founder-badge"[^>]*>.*?<\/span>/s, '').replace(/<a id="redeem-link"[^>]*>.*?<\/a>/s, '')
        .replace(/<a id="redeem-link-setup"[^>]*>.*?<\/a>/s, '').replace(/<div id="redeem-hint">.*?<\/div>/s, '');
      res.writeHead(200, { 'content-type': 'text/html' }); res.end(html); return;
    }
    res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' });
    fs.createReadStream(f).pipe(res); return;
  }
  res.writeHead(404); res.end('nf');
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;
// dw.test resolves to 127.0.0.1 but is NOT "localhost", so the client takes its production branches (same-origin IDUNA, /redeem/)
const browser = await chromium.launch({ args: ['--host-resolver-rules=MAP dw.test 127.0.0.1'] });
const base = `http://dw.test:${port}`;
const errors = [];
async function newPage(ctxOpts = {}, seedStorage) {
  const ctx = await browser.newContext({ viewport: { width: 900, height: 1000 }, ...ctxOpts });
  if (seedStorage) await ctx.addInitScript((kv) => { try { for (const k in kv) localStorage.setItem(k, kv[k]); } catch (e) { /* ignore */ } }, seedStorage);
  await ctx.route('**/fonts.googleapis.com/**', (r) => r.abort());
  await ctx.route('**/fonts.gstatic.com/**', (r) => r.abort());
  const page = await ctx.newPage();
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  // the bridge WebSocket has no server behind it here, so its failure is expected noise
  page.on('console', (m) => { if (m.type() === 'error' && !/Failed to load resource|fonts\.g|WebSocket|ERR_/.test(m.text())) errors.push('console: ' + m.text()); });
  return page;
}
const visible = (page, sel) => page.locator(sel).isVisible();
const text = async (page, sel) => (await page.locator(sel).textContent()).replace(/\s+/g, ' ').trim();

try {
  // ===== a guest connects =====
  tickets = { tickets: 1, founder: false };
  const page = await newPage();
  await page.goto(base + '/DEADWEIGHT/');
  ok((await page.locator('#redeem-link-setup').getAttribute('href')) === '/redeem/' && (await page.locator('#redeem-link-setup').getAttribute('target')) === '_blank',
    'before connecting: the setup panel offers "Redeem it on WOTAN" (same-origin /redeem/, new tab)');
  await page.click('#start-btn');
  await page.waitForFunction(() => document.getElementById('ticket-count').textContent === 'TICKETS: 1');
  ok(await visible(page, '#redeem-link') && (await page.locator('#redeem-link').getAttribute('href')) === '/redeem/' && (await page.locator('#redeem-link').getAttribute('target')) === '_blank',
    'in game: a "Redeem a code" link sits by the ticket count and opens WOTAN\'s page in a new tab');
  ok(!(await visible(page, '#founder-badge')), 'a non-founder sees no FOUNDER badge');
  ok(await visible(page, '#redeem-hint') && /Save progress with IDUNA/.test(await text(page, '#redeem-hint')), 'a guest is told to save progress with IDUNA first, so a code lands on the account being played');
  ok(await visible(page, '#save-btn'), 'the guest still has the Save progress button');
  await page.screenshot({ path: path.join(outdir, '01-guest.png') });

  const [popup] = await Promise.all([page.context().waitForEvent('page'), page.click('#redeem-link')]);
  await popup.waitForLoadState();
  ok(popup.url() === base + '/redeem/', 'clicking it opens /redeem/ in a NEW tab (' + popup.url() + ')');
  ok(/TICKETS: 1/.test(await text(page, '#ticket-count')), 'the game tab is untouched while the other tab is open');
  await popup.close();

  // ===== a code is redeemed over in the other tab; coming back shows it =====
  tickets = { tickets: 9999, founder: true };
  await page.evaluate(() => window.dispatchEvent(new Event('focus')));
  await page.waitForFunction(() => document.getElementById('ticket-count').textContent === 'TICKETS: 9999');
  ok(await visible(page, '#founder-badge') && (await text(page, '#founder-badge')) === 'FOUNDER', 'back on the tab: the balance reads 9999 and the gold FOUNDER badge appears, no reload');
  const gold = await page.locator('#founder-badge').evaluate((e) => getComputedStyle(e).color);
  ok(gold === 'rgb(255, 200, 60)', 'the badge is the native client\'s gold (' + gold + ')');
  await page.screenshot({ path: path.join(outdir, '02-founder.png') });

  const before = ticketReads;
  await page.evaluate(() => { for (let i = 0; i < 3; i++) { window.dispatchEvent(new Event('focus')); document.dispatchEvent(new Event('visibilitychange')); } });
  await page.waitForTimeout(300);
  ok(ticketReads === before, 'a burst of focus/visibility events inside two seconds asks IDUNA nothing more (' + (ticketReads - before) + ' extra reads)');

  // a later return looks again -- and never touches whether the Draft button is enabled
  await page.evaluate(() => { document.getElementById('draft-btn').disabled = true; });
  tickets = { tickets: 0, founder: false };
  await page.waitForTimeout(2100);
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(() => document.getElementById('ticket-count').textContent === 'TICKETS: 0');
  ok(!(await visible(page, '#founder-badge')), 'the badge goes away if the account is not a founder any more');
  ok((await text(page, '#draft-btn')) === 'Draft (no tickets)', 'the Draft button\'s wording follows the new balance');
  ok(await page.locator('#draft-btn').isDisabled(), 'the return-refresh never re-enables the Draft button (the caller may be mid-something)');
  await page.context().close();

  // ===== signed in with IDUNA =====
  tickets = { tickets: 9999, founder: true };
  const sso = await newPage({}, { dw_iduna_sso_token: 'sso.generic.token', dw_iduna_sso_player_id: 'sso-1', dw_iduna_sso_display_name: 'Gary' });
  await sso.goto(base + '/DEADWEIGHT/');
  ok((await text(sso, '#sso-btn')) === 'Continue as Gary', 'sticky IDUNA session: the button offers to continue as the account');
  await sso.click('#sso-btn');
  await sso.waitForFunction(() => document.getElementById('ticket-count').textContent === 'TICKETS: 9999');
  ok(await visible(sso, '#founder-badge'), 'signed in with IDUNA as a founder: 9999 tickets and the FOUNDER badge, straight away');
  ok(!(await visible(sso, '#redeem-hint')) && !(await visible(sso, '#save-btn')), 'an IDUNA account gets no "save progress first" hint and no Save button');
  ok(await visible(sso, '#redeem-link'), 'and still has the Redeem a code link for the next code');
  await sso.context().close();

  // ===== local dev server: no /redeem/ page there, so the link points at production =====
  const dev = await newPage();
  await dev.goto(`http://127.0.0.1:${port}/DEADWEIGHT/`);
  ok((await dev.locator('#redeem-link').getAttribute('href')) === 'https://wotan.okemily.com/redeem/', 'on localhost / 127.0.0.1 the link points at production WOTAN');
  await dev.context().close();

  // ===== a stale cached index.html next to the fresh main.js =====
  staleHtml = true;
  tickets = { tickets: 7, founder: true };
  const stale = await newPage();
  await stale.goto(base + '/DEADWEIGHT/');
  ok((await stale.locator('#founder-badge, #redeem-link, #redeem-link-setup, #redeem-hint').count()) === 0, 'stale page: it really has none of the new elements (test setup)');
  await stale.click('#start-btn');
  await stale.waitForFunction(() => document.getElementById('ticket-count').textContent === 'TICKETS: 7');
  ok(true, 'stale page: the fresh main.js still starts the client and shows the balance -- it loses the badge, never the game');
  await stale.context().close();
  staleHtml = false;

  ok(errors.length === 0, 'no page errors or console errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
} catch (e) {
  fail++;
  console.log('FAIL: test aborted: ' + (e && e.stack || e));
  if (errors.length) console.log('page/console errors so far: ' + errors.join(' | '));
} finally {
  await browser.close();
  server.close();
}
console.log(`\n${pass} passed, ${fail} failed (screenshots in ${outdir})`);
process.exit(fail ? 1 : 0);
