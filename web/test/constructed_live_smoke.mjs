// Live smoke test of DEADWEIGHT constructed mode against a REAL deployment (production or a staging copy): a headless browser opens the
// site as a new guest, builds a deck in the builder (random fill), queues, plays a whole match against whichever constructed bot the server
// pairs it with, and then checks IDUNA recorded it for that guest (matches played, rating moved).
//   NODE_PATH=<dir with node_modules/playwright> PLAYWRIGHT_BROWSERS_PATH=... node web/test/constructed_live_smoke.mjs https://wotan.okemily.com [screenshot-dir]
// It LEAVES BEHIND one guest account and one rated match (against a bot, which moves that bot's rating slightly) -- run it deliberately, not
// in a loop. Guest sign-ups are rate-limited per IP by IDUNA, so a handful of runs per hour is the limit.
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const origin = (process.argv[2] || '').replace(/\/+$/, '');
if (!/^https?:\/\//.test(origin)) { console.error('usage: constructed_live_smoke.mjs <origin, e.g. https://wotan.okemily.com> [screenshot-dir]'); process.exit(2); }
const outdir = process.argv[3] || path.join(process.cwd(), 'constructed-live-shots');
fs.mkdirSync(outdir, { recursive: true });

let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const text = async (page, sel) => (await page.locator(sel).textContent()).replace(/\s+/g, ' ').trim();
const visible = (page, sel) => page.locator(sel).isVisible();

const browser = await chromium.launch();
const errors = [];
try {
  const ctx = await browser.newContext({ viewport: { width: 1000, height: 1100 } });
  await ctx.route('**/fonts.googleapis.com/**', (r) => r.abort());
  await ctx.route('**/fonts.gstatic.com/**', (r) => r.abort());
  const page = await ctx.newPage();
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  page.on('console', (m) => { if (m.type() === 'error' && !/Failed to load resource|fonts\.g|ERR_/.test(m.text())) errors.push('console: ' + m.text()); });
  let playerId = '';
  page.on('response', async (r) => {
    if (r.url().endsWith('/api/v1/games/deadweight/guest-register') && r.ok()) { try { playerId = (await r.json()).player_id || ''; } catch { /* ignore */ } }
  });

  await page.goto(origin + '/DEADWEIGHT/');
  await page.click('#start-btn');
  await page.waitForFunction(() => document.getElementById('status').textContent === 'ready', null, { timeout: 30000 });
  ok(!!playerId, 'a guest account was created (' + playerId + ')');
  ok(await visible(page, '#cons-btn') && !(await page.locator('#cons-btn').isDisabled()), 'the Constructed button is there and enabled');

  await page.click('#cons-btn');
  await page.waitForSelector('#constructed', { state: 'visible' });
  const total = await page.evaluate(async () => (await (await fetch('./src/generated/cards.json')).json()).cards.length);
  ok((await page.locator('#cons-pool .cons-card').count()) === total, `the builder shows the whole catalog (${total} cards)`);
  await page.waitForFunction(() => /YOUR RATING: \d+/.test(document.getElementById('cons-rating').textContent), null, { timeout: 15000 }).catch(() => {});
  const ratingLine = await text(page, '#cons-rating');
  ok(/YOUR RATING: \d+/.test(ratingLine), 'the builder shows the player\'s ladder rating: ' + ratingLine);
  await page.click('#cons-fill');
  ok((await text(page, '#cons-size')) === 'DECK 23/23' && !(await page.locator('#cons-queue').isDisabled()), 'fill-at-random makes a complete 23-card deck and unlocks QUEUE');
  await page.screenshot({ path: path.join(outdir, '01-builder.png') });

  await page.click('#cons-queue');
  await page.waitForSelector('#match', { state: 'visible', timeout: 60000 });
  const opp = await text(page, '#opp-name');
  ok(/\(bot\)/.test(opp), 'matched against a bot: ' + opp);
  ok(/^bot-[a-z]+-c[1-5]\b/.test(opp), 'it is one of the constructed tier bots (bot-<archetype>-c<tier>)');
  for (let i = 0; i < 200; i++) {
    if (await visible(page, '#requeue')) break;
    const pass = page.locator('#pass-btn'), lock = page.locator('#lockin-btn');
    if (await pass.isVisible() && !(await pass.isDisabled())) await pass.click().catch(() => {});
    if (await lock.isVisible() && !(await lock.isDisabled())) await lock.click().catch(() => {});
    await sleep(300);
  }
  ok(await visible(page, '#requeue'), 'the match was played to the end: ' + (await text(page, '#status')));
  await page.screenshot({ path: path.join(outdir, '02-match-over.png') });

  // IDUNA records the result as soon as dw_server reports it
  let prof = null;
  for (let i = 0; i < 20 && !(prof && prof.matches >= 1); i++) {
    await sleep(1000);
    prof = await (await fetch(`${origin}/api/v1/games/deadweight/players/${playerId}/profile`)).json().catch(() => null);
  }
  ok(!!prof && prof.matches >= 1, 'IDUNA recorded the match for the guest: ' + JSON.stringify(prof && { rating: prof.rating, wins: prof.wins, losses: prof.losses, draws: prof.draws, matches: prof.matches }));
  ok(!!prof && prof.rating !== 1500, 'the guest\'s rating moved off 1500 (' + (prof && prof.rating) + ')');
  ok(errors.length === 0, 'no page errors or console errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
} catch (e) {
  fail++;
  console.log('FAIL: smoke aborted: ' + (e && e.stack || e));
  if (errors.length) console.log('page/console errors so far: ' + errors.join(' | '));
} finally {
  await browser.close();
}
console.log(`\n${pass} passed, ${fail} failed (screenshots in ${outdir})`);
process.exit(fail ? 1 : 0);
