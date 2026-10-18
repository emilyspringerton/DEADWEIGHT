// Constructed-mode deck rules, as pure data (no DOM): what a legal deck is, what choosing "2X" of a card does to it, how the card
// pool is filtered and ordered, and how decks are saved. The DOM half is constructed.ts.
//
// A constructed deck has the shape of a draft deck (core/draft.h): 16 distinct cards -- ten of them once (1X), five twice (2X), one
// three times (3X) = 23 cards -- chosen from the whole catalog (the "standard set": every player owns every card, there are no
// packs). dw_server checks the same rule (dw_draft_deck_valid) when the deck is submitted, so this file only has to make building
// one pleasant; it can never make an illegal deck count.

export type Copies = 1 | 2 | 3;
/** card id -> how many copies are in the deck */
export type Deck = Map<number, Copies>;

export const DECK_SIZE = 23;
/** how many cards may take 1X, 2X and 3X (core/draft.h: ten 1-ofs, five 2-ofs, one 3-of) */
export const BUCKETS: readonly [number, number, number] = [10, 5, 1];
export const DECK_CARDS = BUCKETS[0] + BUCKETS[1] + BUCKETS[2];   // 16 distinct cards

export interface CardEntry { id: number; name: string; kind: string; keyword: string; cost: number; power: number; credit: number; text: string }

export function emptyDeck(): Deck { return new Map(); }

/** how many cards each bucket still has room for: [1X, 2X, 3X] */
export function left(deck: Deck): [number, number, number] {
    const used = [0, 0, 0];
    for (const m of deck.values()) used[m - 1]++;
    return [BUCKETS[0] - used[0], BUCKETS[1] - used[1], BUCKETS[2] - used[2]];
}

/** cards in the deck counting copies (0..23) */
export function size(deck: Deck): number {
    let n = 0;
    for (const m of deck.values()) n += m;
    return n;
}

export function isComplete(deck: Deck): boolean {
    const l = left(deck);
    return l[0] === 0 && l[1] === 0 && l[2] === 0;
}

export type SetResult = 'added' | 'changed' | 'removed' | 'full';

/** Choose `m` copies of a card. Choosing the number it already has takes the card out again; choosing another number moves it to that
 *  bucket (freeing the one it was in). 'full' = that bucket has no room, and nothing changed. */
export function setCopies(deck: Deck, id: number, m: Copies): SetResult {
    const had = deck.get(id);
    if (had === m) { deck.delete(id); return 'removed'; }
    if (left(deck)[m - 1] <= 0) return 'full';
    deck.set(id, m);
    return had ? 'changed' : 'added';
}

export function remove(deck: Deck, id: number): boolean { return deck.delete(id); }

/** The 23 card ids to send: each card repeated for its copies, in card-id order (the server shuffles with its own seed). */
export function toCards(deck: Deck): number[] {
    const out: number[] = [];
    for (const id of [...deck.keys()].sort((a, b) => a - b)) for (let i = 0; i < deck.get(id)!; i++) out.push(id);
    return out;
}

/** Read a flat card list back into a deck (as the server's draft deck comes back, or a saved one). null if it is not a legal deck. */
export function fromCards(cards: number[], catalogSize: number): Deck | null {
    if (cards.length !== DECK_SIZE) return null;
    const count = new Map<number, number>();
    for (const c of cards) {
        if (!Number.isInteger(c) || c < 0 || c >= catalogSize) return null;
        count.set(c, (count.get(c) || 0) + 1);
    }
    const deck: Deck = new Map();
    for (const [id, n] of count) { if (n < 1 || n > 3) return null; deck.set(id, n as Copies); }
    return isComplete(deck) ? deck : null;
}

/** Fill whatever room is left with random cards that are not in the deck yet (rand() in [0,1)). A complete deck is untouched. */
export function randomFill(deck: Deck, catalogSize: number, rand: () => number): void {
    const l = left(deck);
    const pool: number[] = [];
    for (let id = 0; id < catalogSize; id++) if (!deck.has(id)) pool.push(id);
    for (let m = 3; m >= 1; m--) {
        for (let k = 0; k < l[m - 1] && pool.length > 0; k++) {
            const i = Math.floor(rand() * pool.length);
            deck.set(pool[i], m as Copies);
            pool.splice(i, 1);
        }
    }
}

// ---- the card pool: filters and order ----

export interface Filter {
    kind: number;          // -1 = any, else the index rules.cardKind gives (0 offense, 1 operations, 2 defense)
    keyword: string;       // '' = any
    cost: number;          // -1 = any
    text: string;          // case-insensitive, matches the name or the rules text
    inDeckOnly: boolean;
}

export const NO_FILTER: Filter = { kind: -1, keyword: '', cost: -1, text: '', inDeckOnly: false };

export function matches(card: CardEntry, kind: number, f: Filter, deck: Deck): boolean {
    if (f.kind >= 0 && kind !== f.kind) return false;
    if (f.keyword && card.keyword !== f.keyword) return false;
    if (f.cost >= 0 && card.cost !== f.cost) return false;
    if (f.inDeckOnly && !deck.has(card.id)) return false;
    const q = f.text.trim().toLowerCase();
    if (q && !(card.name.toLowerCase().includes(q) || (card.text || '').toLowerCase().includes(q) || (card.keyword || '').toLowerCase().includes(q))) return false;
    return true;
}

export type SortKey = 'cost' | 'name' | 'power' | 'kind';

/** card ids in display order; ties always fall back to name, then id, so the order never jumps between renders */
export function sortedIds(cards: CardEntry[], kindOf: (id: number) => number, key: SortKey): number[] {
    const by: Record<SortKey, (c: CardEntry) => number | string> = {
        cost: (c) => c.cost, name: (c) => c.name.toLowerCase(), power: (c) => -c.power, kind: (c) => kindOf(c.id),
    };
    return cards.slice().sort((a, b) => {
        const x = by[key](a), y = by[key](b);
        if (x !== y) return x < y ? -1 : 1;
        const n = a.name.localeCompare(b.name);
        return n !== 0 ? n : a.id - b.id;
    }).map((c) => c.id);
}

// ---- saving ----

export type Saved = { name: string; cards: [number, Copies][] };

export function serialize(deck: Deck): [number, Copies][] {
    return [...deck.entries()].sort((a, b) => a[0] - b[0]);
}

/** Rebuild a deck from stored data. Anything that is not a well-formed list is dropped; entries that no longer fit (a card that is
 *  not in the catalog any more, a bucket that is already full) are skipped, so a stale save degrades to an incomplete deck, never an
 *  error. */
export function deserialize(raw: unknown, catalogSize: number): Deck {
    const deck: Deck = new Map();
    if (!Array.isArray(raw)) return deck;
    for (const e of raw) {
        if (!Array.isArray(e) || e.length !== 2) continue;
        const id = e[0], m = e[1];
        if (!Number.isInteger(id) || id < 0 || id >= catalogSize || (m !== 1 && m !== 2 && m !== 3) || deck.has(id)) continue;
        if (left(deck)[m - 1] > 0) deck.set(id, m);
    }
    return deck;
}

export const MAX_SAVED = 20;
export const MAX_NAME = 24;

export function cleanName(s: string): string {
    return s.replace(/[\u0000-\u001f<>]/g, '').replace(/\s+/g, ' ').trim().slice(0, MAX_NAME);
}

/** Add or replace a saved deck by name (case-insensitive); the oldest are dropped past MAX_SAVED. */
export function upsertSaved(list: Saved[], name: string, deck: Deck): Saved[] {
    const n = cleanName(name);
    if (!n) return list;
    const rest = list.filter((s) => s.name.toLowerCase() !== n.toLowerCase());
    rest.push({ name: n, cards: serialize(deck) });
    return rest.slice(-MAX_SAVED);
}

export function parseSaved(raw: string | null, catalogSize: number): Saved[] {
    if (!raw) return [];
    try {
        const j = JSON.parse(raw);
        if (!Array.isArray(j)) return [];
        const out: Saved[] = [];
        for (const s of j) {
            if (!s || typeof s.name !== 'string') continue;
            const name = cleanName(s.name);
            if (name) out.push({ name, cards: serialize(deserialize(s.cards, catalogSize)) });
        }
        return out.slice(-MAX_SAVED);
    } catch { return []; }
}

/** What is still missing, in words: "PICK 3 MORE: 2 SINGLES, 1 PAIR" -- or '' when the deck is complete. */
export function needs(deck: Deck): string {
    const l = left(deck);
    const more = l[0] + l[1] + l[2];
    if (more === 0) return '';
    const parts: string[] = [];
    if (l[0] > 0) parts.push(`${l[0]} SINGLE${l[0] === 1 ? '' : 'S'} (1X)`);
    if (l[1] > 0) parts.push(`${l[1]} PAIR${l[1] === 1 ? '' : 'S'} (2X)`);
    if (l[2] > 0) parts.push('1 TRIPLE (3X)');
    return `PICK ${more} MORE CARD${more === 1 ? '' : 'S'}: ${parts.join(', ')}`;
}
