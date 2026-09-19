#include "card_text.h"

static const char *const NAME[73] = {
    "Spark", "Bolt", "Railgun", "Plating", "Bulkhead", "Fortress", "Buckler", "Barrier", "Bastion",
    "Call Option", "Overclock", "Infinity Edge", "Rageblade", "Kraken Slayer", "Statikk Shiv", "Liandry's Torment", "Death Mark",
    "Final Spark", "Short Squeeze", "Margin Call", "Pump & Dump", "Flash Crash", "Yolo Roll", "Quick Draw", "Glass Cannon",
    "Piercing Round", "Executioner", "Hail Mary", "Iron Dwarf", "Pressure", "Bribe",
    "Yield Contract", "Thornmail", "Heartsteel", "Dark Pool", "Naked Short", "Hostile Takeover", "Corporate Espionage", "Synthetic Short",
    "Xeno-Biomass", "Sunfire Aegis", "Quartermaster", "Field Cleric", "Capacitor", "Dividend Stock", "Bailout Loan", "Volatility",
    "Merkle Blindness", "Corrosion", "Fire Sale", "Total Blackout", "Realm Warp",
    "Put Option", "Stasis Frame", "Underwrite", "Parametric Policy", "Replacement Cost", "Credit Default Swap", "Reinsurance Bundle",
    "Immortal Shieldbow", "Zhonya's Hourglass", "Seraph's Embrace", "Dead Squares", "Aegis Defender", "Monsoon", "Wish",
    "Bailout Package", "Cold Steel", "Mana Shield", "Corporate Merger", "Glacial Prison", "Chronobreak", "Hush Money"
};

static const char *const TEXT[73] = {
    "", "", "", "", "", "", "", "", "",
    "If it lands: +1 energy.", "You take 4.", "Shields can't reflect it; 4 always lands.", "+1 damage per 2 rounds played.",
    "Costs 2 credits. Every 3rd round: +6 unblockable.", "+3 against Tank.", "If it lands: opp burns 2 for 3 rounds.", "+8 if opp hull is 10 or less.",
    "You take 4.", "+1 per energy opp holds.", "+8 if opp has no credits.", "You take 6.", "All damage halved. Opp loses 2 credits.",
    "50%: +9 damage. Else you take 3.", "+4 if opp passed.", "If you're hit, take 5 more.",
    "Ignores armor.", "+8 if opp hull is 8 or less.", "+11 if your hull is 6 or less.", "+2 damage.", "If it lands: opp loses 1 energy.", "Costs 2 credits.",
    "3+ energy left: +1 energy.", "Reflects half the damage you take.", "Gain armor equal to half damage taken.", "Becomes a mystery exotic.",
    "+3 energy. If hit, take 5 more.", "Cancel opp's card and play it yourself.", "Opp takes 2x their card's cost, unblockable.", "+3 energy if nobody is hurt.",
    "Heal 2 per round for 4 rounds.", "Opp burns 1 per round for 5 rounds.", "+1 energy.", "Heal 2.", "Gain 2 energy.", "+2 credits.",
    "+4 energy, -4 credits.", "Opp loses 3 credits.", "Hide your meters for 3 rounds.", "Lock 1 of opp's cards next round.", "Redraw your hand. +2 credits.",
    "Cancel opp's card. Drain all their energy.", "Costs 3 credits. Swap hands.",
    "Heal up to 3 of the damage you take.", "Immune if your hull is 6 or less.", "+1 energy. If hit for 6+, take 4 more.", "Heal 4 if your hull is 8 or less.",
    "If hit: +3 armor.", "Stops a Burst: +2 energy, +1 credit.", "Immune this round. Next round -2 energy.", "Costs 2 credits. Survive lethal at 1.",
    "Immune this round. Next round -1 energy.", "+4 armor.", "+2 armor.", "+2 armor.", "Reflect all damage taken. Clear burn.", "Heal 6.",
    "Heal 8. Erase your debt.", "Opp played cost 3+: they lose 2 energy.", "Convert up to 4 energy to armor.", "Both hulls become the average.",
    "Lock 2 of opp's cards next round.", "Restore hull to last round's start.", "+2 credits. Opp loses 2."
};

const char *dw_card_name(int id) { return id == -1 ? "PASS" : (id >= 0 && id < 73) ? NAME[id] : "?"; }
const char *dw_card_text(int id) { return (id >= 0 && id < 73) ? TEXT[id] : ""; }
