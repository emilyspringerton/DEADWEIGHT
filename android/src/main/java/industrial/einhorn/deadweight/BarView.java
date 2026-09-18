package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

/** Hull bar with numeric label; optional energy pips drawn under it. */
final class BarView extends View {
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    int value, max = 20, energy = -1;
    int color = 0xFF43A967;
    String label = "";

    BarView(Context c) { super(c); }

    void set(String label, int value, int max, int energy, int color) {
        this.label = label; this.value = value; this.max = Math.max(1, max); this.energy = energy; this.color = color; invalidate();
    }

    @Override protected void onDraw(Canvas cv) {
        float w = getWidth(), h = getHeight(), barH = energy >= 0 ? h * 0.55f : h;
        p.setStyle(Paint.Style.FILL); p.setColor(0xFF333333); cv.drawRoundRect(0, 0, w, barH, 12, 12, p);
        float frac = Math.max(0f, Math.min(1f, value / (float) max));
        p.setColor(color); cv.drawRoundRect(0, 0, w * frac, barH, 12, 12, p);
        p.setColor(0xFFFFFFFF); p.setTextSize(barH * 0.6f); p.setTextAlign(Paint.Align.LEFT);
        cv.drawText(label + "  " + value, 16, barH * 0.72f, p);
        if (energy >= 0) {
            float r = (h - barH) * 0.35f;
            for (int i = 0; i < 6; i++) {
                p.setColor(i < energy ? 0xFFFFD54F : 0xFF444444);
                cv.drawCircle(r + 8 + i * (r * 2.6f), barH + (h - barH) / 2, r, p);
            }
        }
    }
}
