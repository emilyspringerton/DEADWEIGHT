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
    boolean enabled = true, selected = false;

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

    void set(int id, boolean enabled, boolean selected) { cardId = id; this.enabled = enabled; this.selected = selected; invalidate(); }

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
        p.setTextSize(h * 0.36f); cv.drawText(String.valueOf(CardRules.cardPower(cardId)), w / 2, h * 0.58f, p);
        p.setTextSize(h * 0.14f);
        cv.drawText(industrial.einhorn.deadweight.core.MatchModel.kindName(cardId), w / 2, h * 0.86f, p);
        p.setColor(0xCC000000); cv.drawCircle(w * 0.2f, h * 0.17f, h * 0.11f, p);
        p.setColor(0xFFFFD54F); p.setTextSize(h * 0.15f); cv.drawText(String.valueOf(CardRules.cardCost(cardId)), w * 0.2f, h * 0.22f, p);
        if (face == null) for (int i = 0; i <= CardRules.cardTier(cardId); i++) cv.drawCircle(w * (0.62f + 0.12f * i), h * 0.17f, h * 0.04f, p);
    }
}
