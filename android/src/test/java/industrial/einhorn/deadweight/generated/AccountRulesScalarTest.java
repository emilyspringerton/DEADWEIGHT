package industrial.einhorn.deadweight.generated;

/**
 * Real, permanent cross-target check for AccountRulesScalar.java (PARENA/stdlib/deadweight/
 * account_rules_scalar.prn's Java target): brute-forces every plausible input against the same
 * formula core/account_rules.c's own C output implements (min_password_len()==8,
 * is_valid_password_length(len)==(len>=8), is_control_byte(b)==(b<32||b==127)), rather than a
 * vector-file replay (ParityTest.java/FxParityTest.java's own convention) -- disproportionate
 * machinery for two single-branch scalar functions; a direct brute-force over the real input
 * range is the honest, minimal equivalent. Plain JVM, no Android SDK, no JUnit (KARAMBIT
 * convention: main() + counted assertions + nonzero exit on failure).
 */
public final class AccountRulesScalarTest {
    private static int checks = 0;
    private static int failures = 0;

    private static void check(boolean cond, String what) {
        checks++;
        if (!cond) { failures++; System.out.println("FAIL: " + what); }
    }

    public static void main(String[] args) {
        check(AccountRulesScalar.minPasswordLen() == 8, "minPasswordLen() == 8");
        check(AccountRulesScalar.maxDisplayNameLen() == 16, "maxDisplayNameLen() == 16");
        for (int len = 0; len <= 40; len++) {
            check(AccountRulesScalar.isValidPasswordLength(len) == (len >= 8), "isValidPasswordLength(" + len + ")");
        }
        for (int b = -10; b <= 200; b++) {
            check(AccountRulesScalar.isControlByte(b) == (b < 32 || b == 127), "isControlByte(" + b + ")");
        }
        if (failures > 0) {
            System.out.println("AccountRulesScalarTest: " + checks + " checks, " + failures + " FAILURES");
            System.exit(1);
        }
        System.out.println("AccountRulesScalarTest: " + checks + " checks, 0 failures");
    }
}
