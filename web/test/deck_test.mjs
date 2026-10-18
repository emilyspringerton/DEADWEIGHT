// Unit test of the constructed-mode deck rules (web/src/deck.ts, compiled to dist/deck.js): pure data, no DOM, no server.
//   cd web && npx tsc -p tsconfig.json && node test/deck_test.mjs
// The server re-checks every submitted deck with dw_draft_deck_valid (tests/test_server_e2e.sh e2e 5 and tests/test_constructed_wire.py
// drive that); this file pins the CLIENT half: what the builder lets you do, and that a deck it calls complete is one the server takes
// (ten 1-ofs, five 2-ofs, one 3-of = 23 cards, 16 distinct, ids inside the catalog).
import assert from 'node:assert/strict';
import * as D from '../dist/deck.js';

let pass = 0;
const t = (name, fn) => { try { fn(); pass++; console.log('PASS: ' + name); } catch (e) { console.log('FAIL: ' + name + '\n  ' + (e && e.message)); process.exitCode = 1; } };

const N = 100;   // a catalog size
// a deterministic generator, so a failure reproduces
const lcg = (seed) => () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
// the server's rule, written independently of deck.ts: buckets of exactly 10 / 5 / 1, ids in range
const legal = (cards) => {
  if (cards.length !== 23) return false;
  const n = new Map();
  for (const c of cards) { if (!Number.isInteger(c) || c < 0 || c >= N) return false; n.set(c, (n.get(c) || 0) + 1); }
  const by = [0, 0, 0, 0];
  for (const v of n.values()) { if (v > 3) return false; by[v]++; }
  return by[1] === 10 && by[2] === 5 && by[3] === 1;
};

t('an empty deck has all buckets free and is not complete', () => {
  const d = D.emptyDeck();
  assert.deepEqual(D.left(d), [10, 5, 1]);
  assert.equal(D.size(d), 0);
  assert.equal(D.isComplete(d), false);
  assert.equal(D.DECK_SIZE, 23);
  assert.equal(D.DECK_CARDS, 16);
});

t('choosing copies adds, moves between buckets, and choosing the same number again takes the card out', () => {
  const d = D.emptyDeck();
  assert.equal(D.setCopies(d, 7, 2), 'added');
  assert.deepEqual(D.left(d), [10, 4, 1]);
  assert.equal(D.size(d), 2);
  assert.equal(D.setCopies(d, 7, 3), 'changed');            // 2X -> 3X frees the 2X slot
  assert.deepEqual(D.left(d), [10, 5, 0]);
  assert.equal(D.size(d), 3);
  assert.equal(D.setCopies(d, 7, 3), 'removed');            // the same number again = out
  assert.deepEqual(D.left(d), [10, 5, 1]);
  assert.equal(D.size(d), 0);
});

t('a full bucket refuses and changes nothing', () => {
  const d = D.emptyDeck();
  assert.equal(D.setCopies(d, 1, 3), 'added');
  assert.equal(D.setCopies(d, 2, 3), 'full');
  assert.equal(d.has(2), false);
  assert.deepEqual(D.left(d), [10, 5, 0]);
  for (let i = 10; i < 15; i++) D.setCopies(d, i, 2);       // fill the five pairs
  assert.equal(D.setCopies(d, 50, 2), 'full');
  // moving a card INTO a full bucket is refused too, and the card stays where it was
  D.setCopies(d, 60, 1);
  assert.equal(D.setCopies(d, 60, 2), 'full');
  assert.equal(d.get(60), 1);
});

t('remove takes a card out and reports whether it was there', () => {
  const d = D.emptyDeck();
  D.setCopies(d, 4, 1);
  assert.equal(D.remove(d, 4), true);
  assert.equal(D.remove(d, 4), false);
});

t('a complete deck flattens to 23 legal card ids in card-id order, each repeated for its copies', () => {
  const d = D.emptyDeck();
  D.setCopies(d, 90, 3);
  for (let i = 0; i < 5; i++) D.setCopies(d, 20 + i, 2);
  for (let i = 0; i < 10; i++) D.setCopies(d, 40 + i, 1);
  assert.equal(D.isComplete(d), true);
  const cards = D.toCards(d);
  assert.equal(cards.length, 23);
  assert.equal(legal(cards), true);
  assert.deepEqual(cards, [...cards].sort((a, b) => a - b));
  assert.equal(cards.filter((c) => c === 90).length, 3);
  assert.equal(cards.filter((c) => c === 21).length, 2);
  assert.equal(cards.filter((c) => c === 45).length, 1);
});

t('fromCards reads back exactly the legal decks, and nothing else', () => {
  const d = D.emptyDeck();
  D.randomFill(d, N, lcg(1));
  const cards = D.toCards(d);
  const back = D.fromCards(cards, N);
  assert.ok(back);
  assert.deepEqual(D.toCards(back), cards);
  assert.equal(D.fromCards(cards.slice(1), N), null);                       // 22 cards
  assert.equal(D.fromCards([...cards, 0], N), null);                        // 24 cards
  assert.equal(D.fromCards(cards.map((c) => (c === cards[0] ? N : c)), N), null);   // an id outside the catalog
  assert.equal(D.fromCards(cards.map((c, i) => (i === 0 ? -1 : c)), N), null);
  assert.equal(D.fromCards(cards.map((c, i) => (i === 0 ? 1.5 : c)), N), null);
  assert.equal(D.fromCards(new Array(23).fill(3), N), null);                // 23 copies of one card
  // right size, wrong shape: twelve singles, four pairs, one triple = 23 cards, but not ten / five / one
  const shape = [];
  for (let i = 0; i < 12; i++) shape.push(i);
  for (let i = 0; i < 4; i++) shape.push(20 + i, 20 + i);
  shape.push(50, 50, 50);
  assert.equal(shape.length, 23);
  assert.equal(D.fromCards(shape, N), null);
});

t('randomFill completes a partial deck, keeps what was chosen, and any seed gives a legal deck', () => {
  for (let seed = 1; seed <= 200; seed++) {
    const d = D.emptyDeck();
    const rand = lcg(seed);
    // choose a few by hand first: one single, one pair, the triple
    D.setCopies(d, 3, 1); D.setCopies(d, 4, 2); D.setCopies(d, 5, 3);
    D.randomFill(d, N, rand);
    assert.equal(D.isComplete(d), true, `seed ${seed}`);
    assert.equal(d.get(3), 1); assert.equal(d.get(4), 2); assert.equal(d.get(5), 3);
    assert.equal(legal(D.toCards(d)), true, `seed ${seed}`);
  }
});

t('randomFill on a complete deck changes nothing, and on a tiny catalog it fills what it can without throwing', () => {
  const d = D.emptyDeck();
  D.randomFill(d, N, lcg(9));
  const before = D.toCards(d);
  D.randomFill(d, N, lcg(10));
  assert.deepEqual(D.toCards(d), before);
  const small = D.emptyDeck();
  D.randomFill(small, 5, lcg(1));
  assert.equal(small.size, 5);
  assert.equal(D.isComplete(small), false);
});

const cards = [
  { id: 0, name: 'Plasma Lance', kind: 'offense', keyword: 'pierce', cost: 3, power: 5, credit: 0, text: 'Deal 5. Pierce armor.' },
  { id: 1, name: 'Bulwark', kind: 'defense', keyword: '', cost: 2, power: 4, credit: 0, text: 'Block 4.' },
  { id: 2, name: 'Data Siphon', kind: 'operations', keyword: 'siphon', cost: 1, power: 1, credit: 2, text: 'Gain 2 credits.' },
  { id: 3, name: 'Arc Burst', kind: 'offense', keyword: '', cost: 3, power: 6, credit: 0, text: 'Deal 6.' },
];
const kindOf = (id) => ({ offense: 0, operations: 1, defense: 2 })[cards[id].kind];

t('filters combine: kind, keyword, cost, text (name, rules text, keyword) and in-deck-only', () => {
  const deck = D.emptyDeck();
  const m = (f) => cards.filter((c) => D.matches(c, kindOf(c.id), { ...D.NO_FILTER, ...f }, deck)).map((c) => c.id);
  assert.deepEqual(m({}), [0, 1, 2, 3]);
  assert.deepEqual(m({ kind: 0 }), [0, 3]);
  assert.deepEqual(m({ kind: 2 }), [1]);
  assert.deepEqual(m({ keyword: 'pierce' }), [0]);
  assert.deepEqual(m({ cost: 3 }), [0, 3]);
  assert.deepEqual(m({ cost: 3, kind: 0, text: 'arc' }), [3]);
  assert.deepEqual(m({ text: 'BLOCK' }), [1]);                 // case-insensitive, rules text
  assert.deepEqual(m({ text: '  siphon ' }), [2]);              // trimmed; matches the keyword / name
  assert.deepEqual(m({ text: 'no such card' }), []);
  assert.deepEqual(m({ inDeckOnly: true }), []);
  D.setCopies(deck, 2, 1);
  assert.deepEqual(m({ inDeckOnly: true }), [2]);
  assert.deepEqual(m({ inDeckOnly: true, kind: 0 }), []);
});

t('sorting is by the key, ties fall back to name then id, and the order is stable between calls', () => {
  assert.deepEqual(D.sortedIds(cards, kindOf, 'cost'), [2, 1, 3, 0]);      // cost 1, 2, then the two cost-3 cards by name (Arc < Plasma)
  assert.deepEqual(D.sortedIds(cards, kindOf, 'name'), [3, 1, 2, 0]);
  assert.deepEqual(D.sortedIds(cards, kindOf, 'power'), [3, 0, 1, 2]);     // strongest first
  assert.deepEqual(D.sortedIds(cards, kindOf, 'kind'), [3, 0, 2, 1]);      // offense, operations, defense; ties by name
  const a = D.sortedIds(cards, kindOf, 'cost'), b = D.sortedIds(cards, kindOf, 'cost');
  assert.deepEqual(a, b);
  assert.equal(cards[0].id, 0);                                            // sorted a copy: the catalog order is untouched
});

t('serialize / deserialize round-trip, and a stale or hostile save degrades to an incomplete deck instead of throwing', () => {
  const d = D.emptyDeck();
  D.randomFill(d, N, lcg(5));
  const ser = JSON.parse(JSON.stringify(D.serialize(d)));
  assert.deepEqual(D.toCards(D.deserialize(ser, N)), D.toCards(d));
  assert.equal(D.deserialize(null, N).size, 0);
  assert.equal(D.deserialize('x', N).size, 0);
  assert.equal(D.deserialize({ a: 1 }, N).size, 0);
  const junk = [[1, 1], [1, 3], 'z', [2], [2, 1, 1], [-1, 1], [N, 1], [3, 4], [3, 0], [4, 1.5], ['5', 1], null, [6, 2]];
  const out = D.deserialize(junk, N);
  assert.deepEqual([...out.entries()].sort((a, b) => a[0] - b[0]), [[1, 1], [6, 2]]);   // first wins; everything malformed is skipped
  // a save with more cards than the buckets hold: the extras are dropped, never an over-full deck
  const over = [];
  for (let i = 0; i < 20; i++) over.push([i, 3]);
  const o = D.deserialize(over, N);
  assert.equal(o.size, 1);
  assert.ok(D.left(o).every((x) => x >= 0));
});

t('saved decks: names are cleaned, replaced case-insensitively, capped at MAX_SAVED (oldest dropped), and survive bad storage', () => {
  assert.equal(D.cleanName('  my\u0000  <b>deck</b>\n\t one '), 'my bdeck/b one');
  assert.equal(D.cleanName('x'.repeat(100)).length, D.MAX_NAME);
  const d = D.emptyDeck();
  D.setCopies(d, 1, 1);
  let list = D.upsertSaved([], 'Rush', d);
  list = D.upsertSaved(list, 'rush', d);
  assert.equal(list.length, 1);
  assert.equal(list[0].name, 'rush');
  assert.equal(D.upsertSaved(list, '   ', d), list);
  for (let i = 0; i < 30; i++) list = D.upsertSaved(list, 'deck ' + i, d);
  assert.equal(list.length, D.MAX_SAVED);
  assert.equal(list[list.length - 1].name, 'deck 29');
  assert.equal(list[0].name, 'deck 10');
  const back = D.parseSaved(JSON.stringify(list), N);
  assert.deepEqual(back.map((s) => s.name), list.map((s) => s.name));
  assert.deepEqual(D.parseSaved(null, N), []);
  assert.deepEqual(D.parseSaved('{not json', N), []);
  assert.deepEqual(D.parseSaved('{"a":1}', N), []);
  assert.deepEqual(D.parseSaved(JSON.stringify([{ name: 5 }, null, { name: '<>', cards: [] }, { name: 'ok', cards: [[1, 1], [999, 1]] }]), N),
    [{ name: 'ok', cards: [[1, 1]] }]);
});

t('needs() says in words what is still missing, and nothing once the deck is complete', () => {
  const d = D.emptyDeck();
  assert.equal(D.needs(d), 'PICK 16 MORE CARDS: 10 SINGLES (1X), 5 PAIRS (2X), 1 TRIPLE (3X)');
  D.setCopies(d, 1, 3);
  for (let i = 0; i < 4; i++) D.setCopies(d, 10 + i, 2);
  for (let i = 0; i < 9; i++) D.setCopies(d, 30 + i, 1);
  assert.equal(D.needs(d), 'PICK 2 MORE CARDS: 1 SINGLE (1X), 1 PAIR (2X)');
  D.setCopies(d, 60, 1);
  assert.equal(D.needs(d), 'PICK 1 MORE CARD: 1 PAIR (2X)');
  D.setCopies(d, 61, 2);
  assert.equal(D.needs(d), '');
  assert.equal(D.isComplete(d), true);
});

console.log(`\n${pass} passed${process.exitCode ? ', FAILURES above' : ''}`);
