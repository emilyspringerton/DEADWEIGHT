// Constructed-mode deck builder -- the DOM half (rules and persistence shape live in deck.ts, which has no DOM and is unit-tested).
//
// Founder real-time, 2026-10-18: "instead of having to drag the card over to the deck we just need to choose 1 2 or 3 of that card
// and the list updates, its similar to the interface for the draft but you can scroll through and filter the cards; for the current
// cards this is considered the standard set and we wont have any packs ... everyone gets all the cards". So: every card of the
// catalog is in a scrollable pool, each with the draft screen's own 1X / 2X / 3X buttons; the deck list beside it updates as you
// choose; the buckets (ten 1X, five 2X, one 3X) are counted the way the draft counts them. QUEUE unlocks when the deck is complete.
//
// What it does NOT do: talk to the server. It hands the finished 23 card ids to the host (main.ts), which connects in constructed
// mode, submits them (DRAFT_RESUME) and queues.

import * as D from './deck.js';

export interface BuilderHost {
    cards: D.CardEntry[];
    kindOf(id: number): number;
    /** the same card face the draft screen shows (header bar in the kind colour, cost/power line, rules text) */
    cardBox(id: number): string;
    kindColor(kind: number): string;
    kindName(kind: number): string;
    onQueue(cards: number[]): void;
    onBack(): void;
}

const WORK_KEY = 'dw_cons_work';     // the deck being built, so a reload does not lose it
const SAVED_KEY = 'dw_cons_saved';   // named decks
const PREFS_KEY = 'dw_cons_prefs';   // how the pool is sorted

// localStorage can be missing, full, or blocked (private windows, strict settings): the builder works without it, it just forgets.
function read(key: string): string | null { try { return window.localStorage.getItem(key); } catch { return null; } }
function write(key: string, value: string): void { try { window.localStorage.setItem(key, value); } catch { /* not saved */ } }
function parse(raw: string | null): unknown { if (!raw) return null; try { return JSON.parse(raw); } catch { return null; } }

function esc(s: string): string {
    return s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c] as string));
}

const el = (id: string) => document.getElementById(id) as HTMLElement;

export class DeckBuilder {
    private deck: D.Deck;
    private filter: D.Filter = { ...D.NO_FILTER };
    private sort: D.SortKey = 'cost';
    private saved: D.Saved[];
    private tiles = new Map<number, HTMLElement>();
    private shown = 0;

    constructor(private host: BuilderHost) {
        const n = host.cards.length;
        this.deck = D.deserialize(parse(read(WORK_KEY)), n);
        this.saved = D.parseSaved(read(SAVED_KEY), n);
        const prefs = parse(read(PREFS_KEY)) as { sort?: string } | null;
        if (prefs && (prefs.sort === 'cost' || prefs.sort === 'name' || prefs.sort === 'power' || prefs.sort === 'kind')) this.sort = prefs.sort;
        (el('cons-sort') as HTMLSelectElement).value = this.sort;
        this.buildCostOptions();
        this.wire();
        this.renderSaved('');
        this.renderPool();
        this.renderDeck();
    }

    /** the 23 card ids if the deck is complete, else null */
    cards(): number[] | null { return D.isComplete(this.deck) ? D.toCards(this.deck) : null; }

    /** show the player's ladder rating (the server pairs you with the bot nearest it), or nothing */
    setRating(rating: number | null): void {
        el('cons-rating').textContent = rating === null ? '' : `YOUR RATING: ${Math.round(rating)} -- YOU WILL FACE BOTS WITH DECKS NEAR IT`;
    }

    /** a deck from outside (the one just played), e.g. so "edit deck" after a match starts from it */
    load(cards: number[]): void {
        const d = D.fromCards(cards, this.host.cards.length);
        if (!d) return;
        this.deck = d;
        this.changed(true);
    }

    open(): void {
        this.renderPool();
        this.renderDeck();
        el('cons-q').focus({ preventScroll: true });
    }

    // ---- wiring ----

    private buildCostOptions(): void {
        const costs = [...new Set(this.host.cards.map((c) => c.cost))].sort((a, b) => a - b);
        (el('cons-cost') as HTMLSelectElement).innerHTML = '<option value="-1">ANY COST</option>' + costs.map((c) => `<option value="${c}">COST ${c}</option>`).join('');
        const kws = [...new Set(this.host.cards.map((c) => c.keyword).filter((k) => k))].sort();
        (el('cons-kw') as HTMLSelectElement).innerHTML = '<option value="">ANY KEYWORD</option>' + kws.map((k) => `<option value="${esc(k)}">${esc(k.toUpperCase())}</option>`).join('');
    }

    private wire(): void {
        el('cons-pool').addEventListener('click', (e) => {
            const b = (e.target as HTMLElement).closest('button[data-m]') as HTMLButtonElement | null;
            if (!b || b.disabled) return;
            const tile = b.closest('.cons-card') as HTMLElement;
            this.choose(Number(tile.dataset.id), Number(b.dataset.m) as D.Copies);
        });
        el('cons-list').addEventListener('click', (e) => {
            const b = (e.target as HTMLElement).closest('button[data-remove]') as HTMLButtonElement | null;
            if (!b) return;
            D.remove(this.deck, Number(b.dataset.remove));
            this.changed(false);
        });
        el('cons-kinds').addEventListener('click', (e) => {
            const b = (e.target as HTMLElement).closest('button[data-kind]') as HTMLButtonElement | null;
            if (!b) return;
            this.filter.kind = Number(b.dataset.kind);
            for (const x of Array.from(el('cons-kinds').children)) x.setAttribute('aria-pressed', String(x === b));
            this.renderPool();
        });
        (el('cons-kw') as HTMLSelectElement).onchange = (e) => { this.filter.keyword = (e.target as HTMLSelectElement).value; this.renderPool(); };
        (el('cons-cost') as HTMLSelectElement).onchange = (e) => { this.filter.cost = Number((e.target as HTMLSelectElement).value); this.renderPool(); };
        (el('cons-q') as HTMLInputElement).oninput = (e) => { this.filter.text = (e.target as HTMLInputElement).value; this.renderPool(); };
        (el('cons-sort') as HTMLSelectElement).onchange = (e) => {
            this.sort = (e.target as HTMLSelectElement).value as D.SortKey;
            write(PREFS_KEY, JSON.stringify({ sort: this.sort }));
            this.renderPool();
        };
        el('cons-indeck').onclick = () => {
            this.filter.inDeckOnly = !this.filter.inDeckOnly;
            el('cons-indeck').setAttribute('aria-pressed', String(this.filter.inDeckOnly));
            this.renderPool();
        };
        el('cons-reset').onclick = () => {
            this.filter = { ...D.NO_FILTER };
            (el('cons-q') as HTMLInputElement).value = '';
            (el('cons-kw') as HTMLSelectElement).value = '';
            (el('cons-cost') as HTMLSelectElement).value = '-1';
            el('cons-indeck').setAttribute('aria-pressed', 'false');
            for (const x of Array.from(el('cons-kinds').children)) x.setAttribute('aria-pressed', String((x as HTMLElement).dataset.kind === '-1'));
            this.renderPool();
        };
        el('cons-fill').onclick = () => { D.randomFill(this.deck, this.host.cards.length, Math.random); this.changed(true); };
        el('cons-clear').onclick = () => { this.deck = D.emptyDeck(); this.changed(true); };
        (el('cons-saved') as HTMLSelectElement).onchange = (e) => {
            const i = Number((e.target as HTMLSelectElement).value);
            if (!Number.isInteger(i) || !this.saved[i]) return;
            this.deck = D.deserialize(this.saved[i].cards, this.host.cards.length);
            (el('cons-name') as HTMLInputElement).value = this.saved[i].name;
            this.changed(true);
            this.say(`LOADED "${this.saved[i].name}"`);
        };
        el('cons-save').onclick = () => {
            const input = el('cons-name') as HTMLInputElement;
            const name = D.cleanName(input.value) || `DECK ${this.saved.length + 1}`;
            if (D.size(this.deck) === 0) { this.say('NOTHING TO SAVE YET'); return; }
            this.saved = D.upsertSaved(this.saved, name, this.deck);
            write(SAVED_KEY, JSON.stringify(this.saved));
            input.value = name;
            this.renderSaved(String(this.saved.findIndex((s) => s.name.toLowerCase() === name.toLowerCase())));
            this.say(`SAVED "${name}"`);
        };
        el('cons-del').onclick = () => {
            const sel = el('cons-saved') as HTMLSelectElement;
            const i = Number(sel.value);
            if (!Number.isInteger(i) || !this.saved[i]) { this.say('PICK A SAVED DECK TO DELETE'); return; }
            const gone = this.saved[i].name;
            this.saved.splice(i, 1);
            write(SAVED_KEY, JSON.stringify(this.saved));
            this.renderSaved('');
            this.say(`DELETED "${gone}"`);
        };
        el('cons-queue').onclick = () => { const c = this.cards(); if (c) this.host.onQueue(c); };
        el('cons-back').onclick = () => this.host.onBack();
    }

    // ---- state changes ----

    private choose(id: number, m: D.Copies): void {
        const r = D.setCopies(this.deck, id, m);
        if (r === 'full') { this.say(`NO ROOM LEFT FOR ${m}X -- CHOOSE ANOTHER NUMBER OR TAKE A CARD OUT`); return; }
        this.changed(false);
    }

    /** the deck changed: save it, update the list and the tiles (the whole pool when "in deck only" decides what is shown) */
    private changed(rebuildPool: boolean): void {
        write(WORK_KEY, JSON.stringify(D.serialize(this.deck)));
        this.say('');
        if (rebuildPool || this.filter.inDeckOnly) this.renderPool(); else this.refreshTiles();
        this.renderDeck();
    }

    private say(msg: string): void { el('cons-msg').textContent = msg; }

    // ---- drawing ----

    private tileHtml(id: number): string {
        const c = this.host.cards[id];
        const kind = this.host.kindOf(id);
        return `<div class="cons-card" data-id="${id}" data-kind="${kind}">` +
            `<div class="cons-face">${this.host.cardBox(id)}</div>` +
            `<div class="cons-mults" role="group" aria-label="Copies of ${esc(c.name)}">` +
            [1, 2, 3].map((m) => `<button type="button" data-m="${m}" aria-pressed="false" aria-label="${m} cop${m === 1 ? 'y' : 'ies'} of ${esc(c.name)}">${m}X</button>`).join('') +
            `</div></div>`;
    }

    private renderPool(): void {
        const cards = this.host.cards, kindOf = this.host.kindOf;
        const order = D.sortedIds(cards, kindOf, this.sort);
        const ids = order.filter((id) => D.matches(cards[id], kindOf(id), this.filter, this.deck));
        this.shown = ids.length;
        const pool = el('cons-pool');
        pool.innerHTML = ids.length ? ids.map((id) => this.tileHtml(id)).join('') : '<div class="cons-empty">NO CARDS MATCH THESE FILTERS</div>';
        this.tiles = new Map();
        for (const t of Array.from(pool.querySelectorAll('.cons-card')) as HTMLElement[]) this.tiles.set(Number(t.dataset.id), t);
        el('cons-count').textContent = `SHOWING ${ids.length} OF ${cards.length}`;
        this.refreshTiles();
    }

    /** which of each card's 1X/2X/3X is chosen, and which buckets are full */
    private refreshTiles(): void {
        const l = D.left(this.deck);
        for (const [id, tile] of this.tiles) {
            const have = this.deck.get(id);
            tile.classList.toggle('in', have !== undefined);
            for (const b of Array.from(tile.querySelectorAll('button[data-m]')) as HTMLButtonElement[]) {
                const m = Number(b.dataset.m);
                const on = have === m;
                b.classList.toggle('on', on);
                b.setAttribute('aria-pressed', String(on));
                b.disabled = !on && l[m - 1] <= 0;   // a full bucket greys out, the way the draft greys out its buckets
            }
        }
    }

    private renderDeck(): void {
        const l = D.left(this.deck), cap = D.BUCKETS;
        el('cons-left').textContent = `LEFT  1X: ${l[0]}   2X: ${l[1]}   3X: ${l[2]}`;
        el('cons-size').textContent = `DECK ${D.size(this.deck)}/${D.DECK_SIZE}`;
        const groups: string[] = [];
        for (let m = 3; m >= 1; m--) {
            const ids = [...this.deck.entries()].filter(([, c]) => c === m).map(([id]) => id)
                .sort((a, b) => this.host.cards[a].cost - this.host.cards[b].cost || this.host.cards[a].name.localeCompare(this.host.cards[b].name));
            const rows = ids.map((id) => {
                const c = this.host.cards[id], kind = this.host.kindOf(id);
                return `<li data-id="${id}" style="--kc:${this.host.kindColor(kind)}"><span class="cons-n">${m}X</span><span class="cons-nm">${esc(c.name)}</span>` +
                    `<span class="cons-c" title="${esc(this.host.kindName(kind))}">C${c.cost}</span>` +
                    `<button type="button" class="cons-x" data-remove="${id}" aria-label="Take ${esc(c.name)} out">&times;</button></li>`;
            }).join('');
            groups.push(`<div class="cons-grp"><div class="cons-grp-h">${m}X<span>${ids.length}/${cap[m - 1]}</span></div><ul>${rows || '<li class="cons-none">&ndash;</li>'}</ul></div>`);
        }
        el('cons-list').innerHTML = groups.join('');
        const need = D.needs(this.deck);
        const ready = need === '';
        el('cons-need').textContent = ready ? 'DECK READY' : need;
        el('cons-need').classList.toggle('ready', ready);
        (el('cons-queue') as HTMLButtonElement).disabled = !ready;
    }

    private renderSaved(select: string): void {
        const sel = el('cons-saved') as HTMLSelectElement;
        sel.innerHTML = '<option value="">SAVED DECKS' + (this.saved.length ? '' : ' (NONE YET)') + '</option>' +
            this.saved.map((s, i) => {
                const n = s.cards.reduce((a, [, m]) => a + m, 0);
                return `<option value="${i}">${esc(s.name)}${n === D.DECK_SIZE ? '' : ` (${n}/${D.DECK_SIZE})`}</option>`;
            }).join('');
        sel.value = select;
    }

    /** for tests: how many pool tiles are on screen */
    shownCount(): number { return this.shown; }
}
