package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;
import industrial.einhorn.deadweight.generated.CardRules;

/** Card face: NOCK-generated art (res/drawable-nodpi/card_<id>.png) with power/cost overlaid; falls back to a coloured
 *  rounded rect + tier pips if the resource is missing. id < 0 = empty. */
final class CardView extends View {
    private static final int[] KIND_COLOR = {0xFFD9534F, 0xFF5B8DEF, 0xFF43A967}; // burst, tank, shield
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();
    private static final Bitmap[] ART = new Bitmap[9];
    private static final boolean[] ART_TRIED = new boolean[9];
    private static final Paint GREY = new Paint(Paint.ANTI_ALIAS_FLAG);
    static {
        ColorMatrix cm = new ColorMatrix();
        cm.setSaturation(0f);
        GREY.setColorFilter(new ColorMatrixColorFilter(cm));
        GREY.setAlpha(150);
    }
    private final Paint artPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    int cardId = -1;
    boolean enabled = true, selected = false, slotLocked = false;

    CardView(Context c) { super(c); }

    private Bitmap art(int id) {
        if (id < 0 || id >= ART.length) return null;
        synchronized (ART) {
            if (!ART_TRIED[id]) {
                ART_TRIED[id] = true;
                try {
                    int res = getResources().getIdentifier("card_" + id, "drawable", getContext().getPackageName());
                    if (res != 0) ART[id] = BitmapFactory.decodeResource(getResources(), res);
                } catch (RuntimeException e) { ART[id] = null; }
            }
            return ART[id];
        }
    }

    void set(int id, boolean enabled, boolean selected, boolean slotLocked) {
        cardId = id; this.enabled = enabled; this.selected = selected; this.slotLocked = slotLocked; invalidate();
    }

    /** Greedy word-wrap of `text` into at most maxLines lines of width maxW, drawn centred at x. */
    private void wrap(Canvas cv, String text, float x, float y, float maxW, int maxLines) {
        String[] words = text.split(" ");
        StringBuilder line = new StringBuilder();
        int n = 0;
        for (String w : words) {
            String cand = line.length() == 0 ? w : line + " " + w;
            if (p.measureText(cand) > maxW && line.length() > 0) {
                cv.drawText(line.toString(), x, y + n * p.getTextSize() * 1.15f, p);
                if (++n >= maxLines) return;
                line = new StringBuilder(w);
            } else line = new StringBuilder(cand);
        }
        if (line.length() > 0 && n < maxLines) cv.drawText(line.toString(), x, y + n * p.getTextSize() * 1.15f, p);
    }

    @Override protected void onDraw(Canvas cv) {
        float w = getWidth(), h = getHeight(), pad = w * 0.05f;
        r.set(pad, pad, w - pad, h - pad);
        if (cardId < 0) { p.setColor(0x33FFFFFF); p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(3); cv.drawRoundRect(r, 18, 18, p); return; }
        int base = KIND_COLOR[CardRules.cardKind(cardId)];
        Bitmap face = art(cardId);
        p.setStyle(Paint.Style.FILL);
        if (face != null) {
            cv.drawBitmap(face, null, r, enabled ? artPaint : GREY);
        } else {
            p.setColor(enabled ? base : 0xFF555555);
            cv.drawRoundRect(r, 18, 18, p);
        }
        if (selected) { p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(8); p.setColor(0xFFFFFFFF); cv.drawRoundRect(r, 18, 18, p); }
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFFFFFFFF); p.setTextAlign(Paint.Align.CENTER);
        if (cardId < 9) {
            p.setTextSize(h * 0.36f); cv.drawText(String.valueOf(CardRules.cardPower(cardId)), w / 2, h * 0.58f, p);
            p.setTextSize(h * 0.14f);
            cv.drawText(CardText.name(cardId), w / 2, h * 0.86f, p);
        } else {
            // Guild card: name on top, power (if any) in the middle, rule text at the bottom
            p.setTextSize(h * 0.085f); p.setFakeBoldText(true);
            wrap(cv, CardText.name(cardId), w * 0.62f, h * 0.14f, w * 0.5f, 2);
            p.setFakeBoldText(false);
            int pw = CardRules.cardPower(cardId);
            if (pw > 0) { p.setTextSize(h * 0.26f); cv.drawText(String.valueOf(pw), w / 2, h * 0.53f, p); }
            p.setTextSize(h * 0.072f); p.setColor(0xFFEEEEEE);
            wrap(cv, CardText.text(cardId), w / 2, h * 0.66f, w * 0.86f, 5);
        }
        p.setColor(0xCC000000); cv.drawCircle(w * 0.2f, h * 0.17f, h * 0.11f, p);
        p.setColor(0xFFFFD54F); p.setTextSize(h * 0.15f); p.setTextAlign(Paint.Align.CENTER);
        cv.drawText(String.valueOf(CardRules.cardCost(cardId)), w * 0.2f, h * 0.22f, p);
        int credit = CardRules.cardCredit(cardId);
        if (credit > 0) {
            p.setColor(0xCC000000); cv.drawCircle(w * 0.2f, h * 0.39f, h * 0.11f, p);
            p.setColor(0xFF7CFC9A); p.setTextSize(h * 0.12f); cv.drawText("$" + credit, w * 0.2f, h * 0.435f, p);
        }
        if (cardId < 9 && face == null) for (int i = 0; i <= CardRules.cardTier(cardId); i++) cv.drawCircle(w * (0.62f + 0.12f * i), h * 0.17f, h * 0.04f, p);
        if (slotLocked) {
            p.setColor(0xB0000000); cv.drawRoundRect(r, 18, 18, p);
            p.setColor(0xFFFF6E6E); p.setTextSize(h * 0.11f); p.setTextAlign(Paint.Align.CENTER); cv.drawText("LOCKED", w / 2, h * 0.5f, p);
        }
    }
}
