package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;
import industrial.einhorn.deadweight.generated.CardRules;

/** Placeholder card art: a coloured rounded rect (kind), cost pip, power number, tier stripes. id < 0 = empty. */
final class CardView extends View {
    private static final int[] KIND_COLOR = {0xFFD9534F, 0xFF5B8DEF, 0xFF43A967}; // burst, tank, shield
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();
    int cardId = -1;
    boolean enabled = true, selected = false;

    CardView(Context c) { super(c); }

    void set(int id, boolean enabled, boolean selected) { cardId = id; this.enabled = enabled; this.selected = selected; invalidate(); }

    @Override protected void onDraw(Canvas cv) {
        float w = getWidth(), h = getHeight(), pad = w * 0.05f;
        r.set(pad, pad, w - pad, h - pad);
        if (cardId < 0) { p.setColor(0x33FFFFFF); p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(3); cv.drawRoundRect(r, 18, 18, p); return; }
        int base = KIND_COLOR[CardRules.cardKind(cardId)];
        p.setStyle(Paint.Style.FILL);
        p.setColor(enabled ? base : 0xFF555555);
        cv.drawRoundRect(r, 18, 18, p);
        if (selected) { p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(8); p.setColor(0xFFFFFFFF); cv.drawRoundRect(r, 18, 18, p); }
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFFFFFFFF); p.setTextAlign(Paint.Align.CENTER);
        p.setTextSize(h * 0.36f); cv.drawText(String.valueOf(CardRules.cardPower(cardId)), w / 2, h * 0.58f, p);
        p.setTextSize(h * 0.14f);
        cv.drawText(industrial.einhorn.deadweight.core.MatchModel.kindName(cardId), w / 2, h * 0.86f, p);
        p.setColor(0xCC000000); cv.drawCircle(w * 0.2f, h * 0.17f, h * 0.11f, p);
        p.setColor(0xFFFFD54F); p.setTextSize(h * 0.15f); cv.drawText(String.valueOf(CardRules.cardCost(cardId)), w * 0.2f, h * 0.22f, p);
        for (int i = 0; i <= CardRules.cardTier(cardId); i++) cv.drawCircle(w * (0.62f + 0.12f * i), h * 0.17f, h * 0.04f, p);
    }
}
