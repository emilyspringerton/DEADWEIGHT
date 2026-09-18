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

    static int eval(String fn, int a, int c) {
        switch (fn) {
            case "start_hull": return CardRules.startHull();
            case "start_energy": return CardRules.startEnergy();
            case "max_rounds": return CardRules.maxRounds();
            case "hand_size": return CardRules.handSize();
            case "num_cards": return CardRules.numCards();
            case "energy_cap": return CardRules.energyCap(a);
            case "card_kind": return CardRules.cardKind(a);
            case "card_tier": return CardRules.cardTier(a);
            case "card_cost": return CardRules.cardCost(a);
            case "card_power": return CardRules.cardPower(a);
            case "kind_beats": return b(CardRules.kindBeats(a, c));
            case "is_legal_play": return b(CardRules.isLegalPlay(a, c));
            case "damage_dealt": return CardRules.damageDealt(a, c);
            case "round_start_energy": return CardRules.roundStartEnergy(a);
            case "energy_after_play": return CardRules.energyAfterPlay(a, c);
            case "round_winner_by_hull": return CardRules.roundWinnerByHull(a, c);
            case "match_decided": return b(CardRules.matchDecided(a, c));
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
                int a = t.length > 1 ? Integer.parseInt(t[1]) : 0;
                int c = t.length > 2 ? Integer.parseInt(t[2]) : 0;
                int got = eval(t[0], a, c);
                checks++;
                if (got != want) { fails++; System.out.println("FAIL " + line + "  java=" + got); }
            }
        }
        if (checks < 1000) { fails++; System.out.println("FAIL only " + checks + " vectors read"); }
        System.out.println("ParityTest: " + checks + " vectors, " + fails + " failures");
        System.exit(fails == 0 ? 0 : 1);
    }
}
