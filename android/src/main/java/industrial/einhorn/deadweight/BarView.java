package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

/** Hull bar with numeric label; optional energy pips underneath. Brutalist-rendered (flat rects,
 *  PixelFont) to match apps/gui/main.c's meter drawing — see docs/ANDROID_PARITY_NORTHSTAR.md. */
final class BarView extends View {
    private final Paint p = new Paint();
    private final Paint text = new Paint();
    int value, max = 20, energy = -1;
    int color = Theme.GOOD;
    String label = "";

    BarView(Context c) { super(c); text.setColor(Theme.TEXT); }

    void set(String label, int value, int max, int energy, int color) {
        this.label = label; this.value = value; this.max = Math.max(1, max); this.energy = energy; this.color = color; invalidate();
    }

    @Override protected void onDraw(Canvas cv) {
        float w = getWidth(), h = getHeight(), barH = energy >= 0 ? h * 0.55f : h;
        p.setColor(Theme.PANEL); cv.drawRect(0, 0, w, barH, p);
        float frac = Math.max(0f, Math.min(1f, value / (float) max));
        p.setColor(color); cv.drawRect(0, 0, w * frac, barH, p);
        text.setColor(Theme.TEXT);
        PixelFont.draw(cv, text, 8, barH * 0.3f, 2, label + "  " + value);
        if (energy >= 0) {
            float cellH = h - barH, r = cellH * 0.35f;
            for (int i = 0; i < 6; i++) {
                p.setColor(i < energy ? 0xFFFAD246 : Theme.LOCK);
                float cx = r + 8 + i * (r * 2.6f), cy = barH + cellH / 2;
                cv.drawRect(cx - r, cy - r, cx + r, cy + r, p);
            }
        }
    }
}
