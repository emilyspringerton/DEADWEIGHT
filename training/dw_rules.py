"""Python port of PARENA/stdlib/deadweight/card_rules.prn (see docs/CARD_MODE_RULES.md). Kept honest by
test_dw_rules.py, which replays every vector in tests/parity_vectors.txt (generated from the C build).

The per-card DATA tables (kind/tier/cost/power/credit/effect words for the 73 cards) are loaded from that same vector
file at import instead of being retyped here, so they cannot drift from the PARENA source; every FUNCTION (damage,
legality, the effect engine's conditions/amounts, economy, end conditions) is an independent port that the replay test
then checks against the C build's outputs."""
import os

START_HULL, START_ENERGY, START_VAULT, MAX_ROUNDS, HAND_SIZE, NUM_CARDS = 20, 2, 3, 8, 4, 73
_VEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "parity_vectors.txt")
_TABLES = {"card_kind": {}, "card_tier": {}, "card_cost": {}, "card_power": {}, "card_credit": {}, "card_fx_a": {}, "card_fx_b": {}}


def _load():
    with open(_VEC) as f:
        for line in f:
            name = line.split(" ", 1)[0]
            if name in _TABLES:
                lhs, want = line.rsplit(" = ", 1)
                _TABLES[name][int(lhs.split()[1])] = int(want)


_load()


def _idiv(a, b):  # C/Java truncating division
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def energy_cap(e): return 6 if e > 6 else e
def card_kind(i): return _TABLES["card_kind"][i]
def card_tier(i): return _TABLES["card_tier"][i]
def card_cost(i): return 0 if i < 0 else _TABLES["card_cost"][i]
def card_power(i): return 0 if i < 0 else _TABLES["card_power"][i]
def card_credit(i): return _TABLES["card_credit"][i]
def card_fx_a(i): return 0 if i < 9 else _TABLES["card_fx_a"][i]
def card_fx_b(i): return 0 if i < 9 else _TABLES["card_fx_b"][i]
def kind_beats(a, b): return b == (0 if a == 2 else a + 1)
def is_legal_play(i, energy, vault): return 0 <= i < NUM_CARDS and card_cost(i) <= energy and (card_credit(i) == 0 or card_credit(i) <= vault)


def damage_dealt(a, b):
    if a < 0: return 0
    if b < 0: return 0 if card_kind(a) == 2 else card_power(a)
    if card_kind(a) == card_kind(b):
        if card_kind(a) == 0: return card_power(a)
        return _idiv(card_power(a), 2) if card_kind(a) == 1 else 0
    return card_power(a) if kind_beats(card_kind(a), card_kind(b)) else 0


def round_start_energy(e): return energy_cap(e + 2)
def round_start_vault(v): return 99 if v > 98 else v + 1
def energy_after_play(e, played): return energy_cap(e + 1) if played < 0 else e - card_cost(played)
def round_winner_by_hull(h0, h1): return 0 if h0 > h1 else (1 if h0 < h1 else 2)
def round_winner(h0, h1, v0, v1): return 0 if h0 > h1 else 1 if h0 < h1 else 0 if v0 > v1 else 1 if v0 < v1 else 2
def match_decided(h0, h1): return h0 <= 0 or h1 <= 0
def bankrupt(v): return v < -6
def card_substitute(i, roll): return (35 if roll < 25 else 21 if roll < 50 else 20 if roll < 75 else 36) if i == 34 else i


# ---- effect words: decimal CCNNPPMAA ----
def fx_ch(x): return _idiv(x, 10000000)
def fx_nn(x): return _idiv(x, 100000) - _idiv(x, 10000000) * 100
def fx_pp(x): return _idiv(x, 1000) - _idiv(x, 100000) * 100
def fx_m(x): return _idiv(x, 100) - _idiv(x, 1000) * 10
def fx_aa(x): return x - _idiv(x, 100) * 100
def fx_phase(ch): return 1 if ch < 20 else 2


def _cond_ok(x, dealt, taken, mk, ok, oc, mh, oh, el, oe, mv, ov, rnd, roll):
    n, pp = fx_nn(x), fx_pp(x)
    if n == 0: return True
    if n == 1: return dealt > 0
    if n == 2: return taken > 0
    if n == 3: return ok >= 0 and kind_beats(ok, mk)
    if n == 4: return ok >= 0
    if n == 5: return ok < 0
    if n == 6: return ok == pp
    if n == 7: return ok >= 0 and oc >= pp
    if n == 8: return mh <= pp
    if n == 9: return oh <= pp
    if n == 10: return el >= pp
    if n == 11: return oe >= pp
    if n == 12: return mv >= pp
    if n == 13: return mv < pp
    if n == 14: return ov < pp
    if n == 15: return dealt <= 0 and taken <= 0
    if n == 16: return taken >= pp
    if n == 17: return pp > 0 and rnd - _idiv(rnd, pp) * pp == 0
    if n == 18: return roll < pp
    if n == 19: return dealt >= pp
    if n == 20: return ok != pp
    return roll >= pp


def _scale(x, dealt, taken, oc, mh, el, oe, mv, rnd):
    m, a = fx_m(x), fx_aa(x)
    if m == 0: return a
    if m == 1: return a * oe
    if m == 2: return a * oc
    if m == 3: return _idiv(a * rnd, 2)
    if m == 4: return a * el
    if m == 5: return taken if taken < a else a
    if m == 6: return _idiv(taken * a, 10)
    if m == 7: return _idiv(dealt * a, 10)
    if m == 8: return _idiv(a * (20 - mh), 4)
    return _idiv(a * (0 if mv < 0 else mv), 2)


def fx_amount(x, dealt, taken, mk, ok, oc, mh, oh, el, oe, mv, ov, rnd, roll):
    if x == 0: return 0
    return _scale(x, dealt, taken, oc, mh, el, oe, mv, rnd) if _cond_ok(x, dealt, taken, mk, ok, oc, mh, oh, el, oe, mv, ov, rnd, roll) else 0


def legal_mask(hand, energy, vault=START_VAULT, lock_mask=0):
    """5 booleans: slots 0-3 (card present, legal on energy+credits, slot not locked) + pass (always legal)."""
    return [(c >= 0 and not (lock_mask >> i) & 1 and is_legal_play(c, energy, vault)) for i, c in enumerate(hand[:4])] + [True]


def start_hull(): return START_HULL
def start_energy(): return START_ENERGY
def start_vault(): return START_VAULT
def max_rounds(): return MAX_ROUNDS
def hand_size(): return HAND_SIZE
def num_cards(): return NUM_CARDS
