"""Python port of PARENA/stdlib/deadweight/card_rules.prn (see docs/CARD_MODE_RULES.md). Kept honest by
test_dw_rules.py, which replays every vector in tests/parity_vectors.txt (generated from the C build)."""

START_HULL, START_ENERGY, MAX_ROUNDS, HAND_SIZE, NUM_CARDS = 20, 2, 8, 4, 9


def _idiv(a, b):  # C/Java truncating division
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def energy_cap(e): return 6 if e > 6 else e
def card_kind(i): return _idiv(i, 3)
def card_tier(i): return i - _idiv(i, 3) * 3
def card_cost(i): return 1 if card_tier(i) == 0 else (2 if card_tier(i) == 1 else 4)
def card_power(i): return 3 if card_tier(i) == 0 else (6 if card_tier(i) == 1 else 10)
def kind_beats(a, b): return b == (0 if a == 2 else a + 1)
def is_legal_play(i, energy): return i >= 0 and i <= 8 and card_cost(i) <= energy


def damage_dealt(a, b):
    if a < 0: return 0
    if b < 0: return 0 if card_kind(a) == 2 else card_power(a)
    if card_kind(a) == card_kind(b):
        if card_kind(a) == 0: return card_power(a)
        return _idiv(card_power(a), 2) if card_kind(a) == 1 else 0
    return card_power(a) if kind_beats(card_kind(a), card_kind(b)) else 0


def round_start_energy(e): return energy_cap(e + 2)
def energy_after_play(e, played): return energy_cap(e + 1) if played < 0 else e - card_cost(played)
def round_winner_by_hull(h0, h1): return 0 if h0 > h1 else (1 if h0 < h1 else 2)
def match_decided(h0, h1): return h0 <= 0 or h1 <= 0


def legal_mask(hand, energy):
    """5 booleans: slots 0-3 (card legal & present) + pass (always legal)."""
    return [(c >= 0 and is_legal_play(c, energy)) for c in hand[:4]] + [True]


def start_hull(): return START_HULL
def start_energy(): return START_ENERGY
def max_rounds(): return MAX_ROUNDS
def hand_size(): return HAND_SIZE
def num_cards(): return NUM_CARDS
