package industrial.einhorn.deadweight;

import industrial.einhorn.deadweight.generated.CardRules;
import java.io.BufferedReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * Cross-target parity: replays every vector the PARENA->C build produced (tests/parity_vectors.txt) through the
 * PARENA->Java build and requires identical results. Plain JVM, no Android SDK, no JUnit (KARAMBIT convention:
 * main() + counted assertions + nonzero exit on failure). Usage: ParityTest <path/to/parity_vectors.txt>
 */
public final class ParityTest {
    private static int b(boolean v) { return v ? 1 : 0; }

    static int eval(String fn, int[] a) {
        switch (fn) {
            case "start_hull": return CardRules.startHull();
            case "start_energy": return CardRules.startEnergy();
            case "start_vault": return CardRules.startVault();
            case "max_rounds": return CardRules.maxRounds();
            case "hand_size": return CardRules.handSize();
            case "num_cards": return CardRules.numCards();
            case "energy_cap": return CardRules.energyCap(a[0]);
            case "card_kind": return CardRules.cardKind(a[0]);
            case "card_tier": return CardRules.cardTier(a[0]);
            case "card_cost": return CardRules.cardCost(a[0]);
            case "card_power": return CardRules.cardPower(a[0]);
            case "card_credit": return CardRules.cardCredit(a[0]);
            case "card_fx_a": return CardRules.cardFxA(a[0]);
            case "card_fx_b": return CardRules.cardFxB(a[0]);
            case "card_keyword": return CardRules.cardKeyword(a[0]);
            case "card_substitute": return CardRules.cardSubstitute(a[0], a[1]);
            case "kind_beats": return b(CardRules.kindBeats(a[0], a[1]));
            case "is_legal_play": return b(CardRules.isLegalPlay(a[0], a[1], a[2]));
            case "damage_dealt": return CardRules.damageDealt(a[0], a[1]);
            case "round_start_energy": return CardRules.roundStartEnergy(a[0]);
            case "round_start_vault": return CardRules.roundStartVault(a[0]);
            case "energy_after_play": return CardRules.energyAfterPlay(a[0], a[1]);
            case "round_winner_by_hull": return CardRules.roundWinnerByHull(a[0], a[1]);
            case "round_winner": return CardRules.roundWinner(a[0], a[1], a[2], a[3]);
            case "match_decided": return b(CardRules.matchDecided(a[0], a[1]));
            case "bankrupt": return b(CardRules.bankrupt(a[0]));
            case "fx_ch": return CardRules.fxCh(a[0]);
            case "fx_phase": return CardRules.fxPhase(a[0]);
            case "fx_amount":
                return CardRules.fxAmount(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13]);
            default: throw new IllegalArgumentException("unknown vector fn " + fn);
        }
    }

    public static void main(String[] args) throws IOException {
        if (args.length != 1) { System.err.println("usage: ParityTest <parity_vectors.txt>"); System.exit(2); }
        int checks = 0, fails = 0;
        try (BufferedReader r = Files.newBufferedReader(Paths.get(args[0]))) {
            String line;
            while ((line = r.readLine()) != null) {
                int eq = line.indexOf(" = ");
                if (eq < 0) { fails++; System.out.println("FAIL malformed: " + line); continue; }
                int want = Integer.parseInt(line.substring(eq + 3).trim());
                String[] t = line.substring(0, eq).trim().split(" ");
                int[] a = new int[16];
                for (int i = 1; i < t.length && i <= 16; i++) a[i - 1] = Integer.parseInt(t[i]);
                int got = eval(t[0], a);
                checks++;
                if (got != want) { fails++; System.out.println("FAIL " + line + "  java=" + got); }
            }
        }
        if (checks < 10000) { fails++; System.out.println("FAIL only " + checks + " vectors read"); }
        System.out.println("ParityTest: " + checks + " vectors, " + fails + " failures");
        System.exit(fails == 0 ? 0 : 1);
    }
}
