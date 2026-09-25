package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;
import industrial.einhorn.deadweight.generated.CardRules;

/** Card face, brutalist-rendered to match apps/gui/main.c's card_box(): flat rects, a 2px frame,
 *  the shared 5x7 PixelFont — no art bitmaps. id < 0 = empty ("PASS") slot. Retires the old
 *  NOCK card_<id>.png art from the live render path per docs/ANDROID_PARITY_NORTHSTAR.md Phase 1
 *  (BRAND_STYLE_GUIDE.md Section 2A is now Android's target visual language too). */
final class CardView extends View {
    private final Paint fill = new Paint();
    private final Paint text = new Paint();
    int cardId = -1;
    boolean enabled = true, selected = false, slotLocked = false;

    CardView(Context c) { super(c); text.setColor(Theme.TEXT); }

    void set(int id, boolean enabled, boolean selected, boolean slotLocked) {
        cardId = id; this.enabled = enabled; this.selected = selected; this.slotLocked = slotLocked; invalidate();
    }

    private void rect(Canvas cv, float x, float y, float w, float h, int color) {
        fill.setStyle(Paint.Style.FILL); fill.setColor(color);
        cv.drawRect(x, y, x + w, y + h, fill);
    }

    private void frame(Canvas cv, float x, float y, float w, float h, int color, float t) {
        rect(cv, x, y, w, t, color); rect(cv, x, y + h - t, w, t, color);
        rect(cv, x, y, t, h, color); rect(cv, x + w - t, y, t, h, color);
    }

    /** Greedy word-wrap into lines of at most `per` chars — mirrors apps/gui/main.c's wrap_next/wrap_lines. */
    private static String[] wrap(String s, int per) {
        if (per < 1) per = 1;
        java.util.List<String> lines = new java.util.ArrayList<>();
        StringBuilder line = new StringBuilder();
        for (String w : s.split(" ")) {
            String cand = line.length() == 0 ? w : line + " " + w;
            if (cand.length() > per && line.length() > 0) { lines.add(line.toString()); line = new StringBuilder(w); }
            else line = new StringBuilder(cand);
        }
        if (line.length() > 0) lines.add(line.toString());
        return lines.toArray(new String[0]);
    }

    @Override protected void onDraw(Canvas cv) {
        float w = getWidth(), h = getHeight();
        if (cardId < 0) {
            rect(cv, 0, 0, w, h, Theme.PANEL);
            frame(cv, 0, 0, w, h, Theme.LOCK, 2);
            text.setColor(Theme.DIM);
            PixelFont.drawCentered(cv, text, w / 2, h / 2 - 7, 2, "PASS");
            return;
        }
        rect(cv, 0, 0, w, h, Theme.PANEL);
        int kc = Theme.KIND_COLOR[CardRules.cardKind(cardId)];
        if (!enabled) kc = Theme.dim(kc, 3);
        int tc = enabled ? Theme.TEXT : Theme.DIM;
        boolean big = w >= 200;
        float sc = big ? 2 : 1;

        // header: kind-colored band with the name
        float headerH = big ? 30 : 22;
        rect(cv, 0, 0, w, headerH, kc);
        text.setColor(Theme.TEXT);
        String nm = CardText.name(cardId);
        float nameScale = sc;
        if (PixelFont.width(nm, nameScale) > w - 8) {
            while (nameScale > 1 && PixelFont.width(nm, nameScale) > w - 8) nameScale -= 0.5f;
            if (PixelFont.width(nm, nameScale) > w - 8) {
                int fit = (int) ((w - 8) / (6 * nameScale));
                nm = fit > 0 ? nm.substring(0, Math.min(nm.length(), fit)) : "";
            }
        }
        PixelFont.drawCentered(cv, text, w / 2, big ? 8 : 6, nameScale, nm);

        float ty;
        text.setColor(tc);
        if (big) {
            PixelFont.drawCentered(cv, text, w / 2, headerH + 6, 2, "COST " + CardRules.cardCost(cardId) + "  PWR " + CardRules.cardPower(cardId));
            text.setColor(Theme.DIM);
            String kwLine = Theme.KIND_NAME[CardRules.cardKind(cardId)];
            String credit = CardRules.cardCredit(cardId) > 0 ? " $" : "";
            PixelFont.drawCentered(cv, text, w / 2, headerH + 24, 1, kwLine + credit);
            ty = headerH + 38;
        } else {
            PixelFont.drawCentered(cv, text, w / 2, headerH + 6, 1, "COST " + CardRules.cardCost(cardId) + " PWR " + CardRules.cardPower(cardId));
            ty = headerH + 18;
        }

        String body = CardText.text(cardId);
        if (!body.isEmpty()) {
            text.setColor(Theme.DIM);
            int per = (int) ((w - 8) / (6 * sc));
            String[] lines = wrap(body, per);
            float lh = sc * 9;
            for (String ln : lines) {
                if (ty + 8 * sc > h - 2) break;
                PixelFont.draw(cv, text, 4, ty, sc, ln);
                ty += lh;
            }
        }

        if (selected) frame(cv, 0, 0, w, h, Theme.SEL, big ? 4 : 3);
        if (slotLocked) {
            rect(cv, 0, 0, w, h, 0xB0000000);
            text.setColor(Theme.BAD);
            PixelFont.drawCentered(cv, text, w / 2, h / 2 - 4, sc, "LOCKED");
        }
    }
}
