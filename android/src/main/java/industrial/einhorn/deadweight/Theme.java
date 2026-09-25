package industrial.einhorn.deadweight;

/** Brutalist desktop palette, ported verbatim from apps/gui/main.c's Col constants (S513
 *  BRAND_STYLE_GUIDE.md Section 2A is the canonical source; do not tint or "soften" these for
 *  Android — the whole point of this parity pass is that both clients render the same colors). */
final class Theme {
    private Theme() {}

    static final int BG = 0xFF12141C;
    static final int PANEL = 0xFF20242F;
    static final int TEXT = 0xFFEBEBF0;
    static final int DIM = 0xFF82879C;
    static final int GOOD = 0xFF50C878;
    static final int BAD = 0xFFE15046;
    static final int SEL = 0xFFFFFFFF;
    static final int LOCK = 0xFF5A5A69;

    /** Offense (red), Operations (yellow), Defense (blue) — index by CardRules.cardKind(id). */
    static final int[] KIND_COLOR = {0xFFD7463C, 0xFFE1A028, 0xFF468CE6};
    static final String[] KIND_NAME = {"OFFENSE", "OPERATIONS", "DEFENSE"};

    static int dim(int color, int div) {
        int a = color & 0xFF000000;
        int r = ((color >> 16) & 0xFF) / div, g = ((color >> 8) & 0xFF) / div, b = (color & 0xFF) / div;
        return a | (r << 16) | (g << 8) | b;
    }
}
