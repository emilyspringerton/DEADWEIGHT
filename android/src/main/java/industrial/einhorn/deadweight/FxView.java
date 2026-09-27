package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.os.SystemClock;
import android.view.View;
import industrial.einhorn.deadweight.core.FxTimeline;
import industrial.einhorn.deadweight.core.MatchModel;
import java.util.ArrayList;
import java.util.List;

/**
 * The match screen's arena band: plays one round's reveal on the timeline {@link FxTimeline} computed from the
 * PARENA-generated fx decision layer (S555 phase 2, docs/ANDROID_PARITY_NORTHSTAR.md). "PARENA decides, host
 * renders": which scenario, who won, crit, and every stage boundary come from FxRules -- identical to
 * apps/gui/fx.c and web/src/fx.ts -- while the drawing here is Android Canvas in the desktop's brutalist style
 * (flat rects, 2px frames, PixelFont, fx.c's own label colours and wording). Deliberately simpler than the SDL2
 * client: no particles, ships, projectiles or audio -- a named gap in the parity doc, not a silent omission.
 */
final class FxView extends View {
    // apps/gui/fx.c's C3 label colours, verbatim.
    private static final int RED = 0xFFE15042, YEL = 0xFFF5BE37, CYAN = 0xFF82E1FF, SIL = 0xFFBEC6D2,
        GRN = 0xFF5AD782, GOLD = 0xFFFFD750, GRY = 0xFF6E7280, SICK = 0xFF82DC5A;
    /** fx.c's bypass labels, indexed by card keyword (0 = none). */
    private static final String[] BYPASS = {"BYPASS", "LOCKED", "SABOTAGE", "FLANKED", "EXPOSED", "SIPHON"};

    private static final class Label {
        final String s; final int color, seat; final float scale, y; final int t0, life;
        /** seat 0 = over your card, 1 = over theirs, -1 = centred. y is a fraction of view height. */
        Label(String s, int color, int seat, float scale, float y, int t0, int life) {
            this.s = s; this.color = color; this.seat = seat; this.scale = scale; this.y = y; this.t0 = t0; this.life = life;
        }
    }

    private final Paint fill = new Paint(), text = new Paint();
    private final List<Label> labels = new ArrayList<>();
    private MatchModel.RoundLog round;
    private FxTimeline tl;
    private long startMs;

    FxView(Context c) { super(c); }

    /** Starts (or, for the same round re-bound after a re-render, continues) the reveal. startMs = when the
     *  ROUND_RESULT arrived, so rebuilding the screen mid-animation doesn't restart it. */
    void set(MatchModel.RoundLog r, FxTimeline t, long startMs) {
        round = r; tl = t; this.startMs = startMs;
        labels.clear();
        if (r != null && t != null) buildLabels(r, t);
        invalidate();
    }

    private void buildLabels(MatchModel.RoundLog r, FxTimeline t) {
        int c0 = t.clash0;
        switch (t.scenario) {
            case FxTimeline.SC_BLITZ: labels.add(new Label("INTERRUPT", RED, t.lose, 2, 0.55f, c0 + 620, 900)); break;
            case FxTimeline.SC_BLOCK:
                labels.add(new Label((t.lose == 0 ? r.dmgYou : r.dmgOpp) > 0 ? "REFLECTED" : "ABSORBED", CYAN, t.win, 2, 0.55f, c0 + 700, 950)); break;
            case FxTimeline.SC_BYPASS: {
                int d = t.kw == 4 ? 900 : t.kw == 3 ? 1150 : 1000;
                labels.add(new Label(BYPASS[Math.max(0, Math.min(5, t.kw))], YEL, t.lose, 2, 0.55f, c0 + d, 1000)); break;
            }
            case FxTimeline.SC_MIRROR_DEFENSE: labels.add(new Label("STALEMATE", CYAN, -1, 2, 0.45f, c0 + 700, 800)); break;
            case FxTimeline.SC_HOLD_SHIELD: labels.add(new Label("HOLD", CYAN, t.win, 2, 0.55f, c0 + 400, 800)); break;
            case FxTimeline.SC_BOTH_PASS: labels.add(new Label("BOTH HOLD  +1 ENERGY", GRY, -1, 2, 0.45f, c0 + 150, 600)); break;
            case FxTimeline.SC_UNOPPOSED: labels.add(new Label("UNOPPOSED", RED, t.lose, 2, 0.55f, c0 + 500, 800)); break;
            default: break;
        }
        if (t.crit) labels.add(new Label("CRITICAL", GOLD, -1, 3, 0.08f, c0 + 60, t.clash1 - c0));
        int[] flags = {r.flagsYou, r.flagsOpp}, dmg = {r.dmgYou, r.dmgOpp}, heal = {r.healYou, r.healOpp};
        int[] armorD = {t.armorDeltaYou, t.armorDeltaOpp}, vaultD = {t.vaultDeltaYou, t.vaultDeltaOpp};
        for (int s = 0; s < 2; s++) {
            if ((flags[s] & 1) != 0) labels.add(new Label("CANCELLED", GRY, s, 2, 0.30f, c0 + 300, 1000));
            if ((flags[s] & 2) != 0) labels.add(new Label("IMMUNE", GOLD, s, 2, 0.70f, c0 + 900, 900));
            if ((flags[s] & 4) != 0) labels.add(new Label("LIFELINE", RED, s, 2, 0.70f, c0 + 1000, 900));
            if ((flags[s] & 128) != 0) labels.add(new Label("COPIED", CYAN, s, 2, 0.62f, c0 + 500, 900));
            if ((flags[s] & 8) != 0) labels.add(new Label("DISABLED", GRY, 1 - s, 2, 0.70f, t.stat0 + 100, 900));
            if (t.hasHull && dmg[s] > 0) labels.add(new Label("-" + dmg[s], RED, s, dmg[s] >= 8 ? 4 : 3, 0.02f, t.hull0 + 40, 900));
            if (t.hasHull && heal[s] > 0) labels.add(new Label("+" + heal[s], GRN, s, 3, 0.35f, t.hull0 + (dmg[s] > 0 ? 390 : 40), 900));
            if (t.hasArmor && armorD[s] > 0) labels.add(new Label("+" + armorD[s] + " ARMOR", SIL, s, 2, 0.40f, t.armor0 + 150, 850));
            if (t.hasArmor && armorD[s] < 0) labels.add(new Label("-" + (-armorD[s]) + " ARMOR", SICK, s, 2, 0.40f, t.armor0 + 90, 900));
            if (t.hasEcon && vaultD[s] != 0) labels.add(new Label((vaultD[s] > 0 ? "+$" : "-$") + Math.abs(vaultD[s]), GOLD, s, 2, 0.40f, t.econ0 + 60, 850));
        }
        if (((r.flagsYou | r.flagsOpp) & 16) != 0) labels.add(new Label("HANDS SWAPPED", YEL, -1, 2, 0.45f, t.stat0 + 100, 900));
    }

    private void rect(Canvas cv, float x, float y, float w, float h, int color) {
        fill.setStyle(Paint.Style.FILL); fill.setColor(color);
        cv.drawRect(x, y, x + w, y + h, fill);
    }

    private void frame(Canvas cv, float x, float y, float w, float h, int color, float th) {
        rect(cv, x, y, w, th, color); rect(cv, x, y + h - th, w, th, color);
        rect(cv, x, y, th, h, color); rect(cv, x + w - th, y, th, h, color);
    }

    private static int alpha(int color, float a) {
        int al = Math.max(0, Math.min(255, (int) (((color >>> 24) & 0xFF) * a)));
        return (al << 24) | (color & 0x00FFFFFF);
    }

    private float chipX(int seat, float w, float cw) { return seat == 0 ? w * 0.06f : w - cw - w * 0.06f; }

    /** One small reveal card, desktop card_box() header style: kind band + name, "PASS" when nothing resolved. */
    private void chip(Canvas cv, int seat, float x, float y, float cw, float ch, float flip, boolean dimmed) {
        int id = seat == 0 ? tl.cardYouId : tl.cardOppId, kind = seat == 0 ? tl.kindYou : tl.kindOpp;
        cv.save();
        cv.scale(Math.max(0.02f, flip), 1, x + cw / 2, y + ch / 2);
        rect(cv, x, y, cw, ch, Theme.PANEL);
        float sc = cw >= 200 ? 2 : 1;
        if (id < 0 || kind < 0) {
            frame(cv, x, y, cw, ch, Theme.LOCK, 2);
            text.setColor(Theme.DIM);
            PixelFont.drawCentered(cv, text, x + cw / 2, y + ch / 2 - 4 * sc, sc, "PASS");
        } else {
            int kc = Theme.KIND_COLOR[kind];
            float hh = 12 * sc + 6;
            rect(cv, x, y, cw, hh, kc);
            frame(cv, x, y, cw, ch, kc, 2);
            text.setColor(Theme.TEXT);
            String nm = CardText.name(id);
            int fit = (int) ((cw - 8) / (6 * sc));
            if (nm.length() > fit) nm = nm.substring(0, Math.max(0, fit));
            PixelFont.drawCentered(cv, text, x + cw / 2, y + 3 + 2 * sc, sc, nm);
            text.setColor(Theme.DIM);
            PixelFont.drawCentered(cv, text, x + cw / 2, y + hh + 8, 1, Theme.KIND_NAME[kind]);
        }
        if (dimmed) rect(cv, x, y, cw, ch, 0x90000000);
        cv.restore();
    }

    @Override protected void onDraw(Canvas cv) {
        float w = getWidth(), h = getHeight();
        rect(cv, 0, 0, w, h, Theme.BG);
        frame(cv, 0, 0, w, h, Theme.PANEL, 2);
        if (round == null || tl == null) return;
        int t = (int) (SystemClock.uptimeMillis() - startMs);
        boolean running = t < tl.totalMs;
        if (!running) t = tl.totalMs;

        // Crit shake: a short, decaying jolt at impact (fx.c shakes on every clash; kept to crits here so the
        // small phone band stays readable).
        float dx = 0;
        if (tl.crit && t >= tl.clash0 && t < tl.clash0 + 300) dx = (float) Math.sin(t * 0.11) * 6 * (1 - (t - tl.clash0) / 300f);
        cv.save();
        cv.translate(dx, 0);

        float cw = Math.min(w * 0.34f, 280), ch = h * 0.52f, cy = h * 0.2f;
        float flip = Math.min(1f, t / (float) Math.max(1, tl.clash0));
        boolean decided = t >= tl.clash0 + 400;
        for (int s = 0; s < 2; s++) chip(cv, s, chipX(s, w, cw), cy, cw, ch, flip, decided && tl.lose == s);
        if (decided && tl.win >= 0) frame(cv, chipX(tl.win, w, cw) - 4, cy - 4, cw + 8, ch + 8, Theme.SEL, 3);
        text.setColor(Theme.DIM);
        PixelFont.drawCentered(cv, text, w / 2, cy + ch / 2 - 4, 2, "VS");
        PixelFont.drawCentered(cv, text, chipX(0, w, cw) + cw / 2, cy + ch + 6, 1, "YOU");
        PixelFont.drawCentered(cv, text, chipX(1, w, cw) + cw / 2, cy + ch + 6, 1, "THEM");

        // Impact flash, in the winner's kind colour (white on a no-winner clash).
        int f0 = tl.clash0 + 60;
        if (t >= f0 && t < f0 + 180) {
            int wk = tl.win == 0 ? tl.kindYou : tl.win == 1 ? tl.kindOpp : -1;
            rect(cv, 0, 0, w, h, alpha(wk >= 0 ? Theme.KIND_COLOR[wk] : Theme.SEL, 0.35f * (1 - (t - f0) / 180f)));
        }

        for (Label l : labels) {
            if (t < l.t0) continue;
            float age = (t - l.t0) / (float) Math.max(1, l.life);
            // Labels that finished before the timeline ended fade out; at rest the final state keeps them faint
            // so the last round stays readable until the next reveal starts.
            float a = running ? (age >= 1 ? 0 : age < 0.8f ? 1 : (1 - age) / 0.2f) : 0.35f;
            if (a <= 0) continue;
            float lx = l.seat < 0 ? w / 2 : chipX(l.seat, w, cw) + cw / 2;
            float ly = h * l.y - (running ? Math.min(age, 1) * 10 : 10);
            text.setColor(alpha(l.color, a));
            PixelFont.drawCentered(cv, text, lx, ly, l.scale, l.s);
        }
        cv.restore();

        // Stage strip: which resource stages this round has, and which one is playing (fx.c's timeline table).
        String[] names = {"HULL", "ARMOR", "ECON", "STATUS"};
        boolean[] has = {tl.hasHull, tl.hasArmor, tl.hasEcon, tl.hasStat};
        int[] s0 = {tl.hull0, tl.armor0, tl.econ0, tl.stat0}, s1 = {tl.hull1, tl.armor1, tl.econ1, tl.stat1};
        float sw = (w - 20) / 4f, sy = h - 20;
        for (int i = 0; i < 4; i++) {
            float sx = 10 + i * sw;
            boolean on = has[i] && t >= s0[i] && t < s1[i], done = has[i] && t >= s1[i];
            rect(cv, sx + 2, sy, sw - 4, 14, on ? Theme.TEXT : done ? Theme.LOCK : Theme.PANEL);
            text.setColor(on ? Theme.BG : has[i] ? Theme.TEXT : Theme.DIM);
            PixelFont.drawCentered(cv, text, sx + sw / 2, sy + 3, 1, names[i]);
        }
        text.setColor(Theme.DIM);
        PixelFont.draw(cv, text, 8, 8, 1, "R" + round.round + "  " + tl.scenarioName().toUpperCase());

        if (running) postInvalidateOnAnimation();
    }
}
