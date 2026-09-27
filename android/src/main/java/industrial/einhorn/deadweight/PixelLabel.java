package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.Gravity;
import android.view.View;

/** One line of the shared 5x7 PixelFont (see PixelFont.java) as its own View, self-sizing from
 *  text+scale so it drops into a LinearLayout like any other row -- the brutalist-menu equivalent
 *  of a TextView, used everywhere apps/gui/main.c's text()/text_c() draw a menu/title/status line
 *  (docs/ANDROID_PARITY_NORTHSTAR.md's menu-parity pass; CardView/BarView already do the same for
 *  in-match UI). System fonts never appear on this screen -- that inconsistency (bitmap font in
 *  the arena, system font on the menu) was the actual gap the founder's "look just like our
 *  windows app" ask named. */
final class PixelLabel extends View {
    private final Paint paint = new Paint();
    private String text = "";
    private float scale = 2;
    private int gravity = Gravity.START;

    PixelLabel(Context c) { super(c); }

    PixelLabel(Context c, String text, float scale, int color) {
        super(c);
        set(text, scale, color);
    }

    void set(String text, float scale, int color) {
        this.text = text == null ? "" : text;
        this.scale = scale;
        paint.setColor(color);
        requestLayout();
        invalidate();
    }

    void setColor(int color) { paint.setColor(color); invalidate(); }

    /** Gravity.START (left, default) or Gravity.CENTER_HORIZONTAL -- matches text() vs text_c(). */
    void setGravity(int g) { gravity = g; invalidate(); }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
        int wantW = (int) Math.ceil(PixelFont.width(text, scale));
        int wantH = (int) Math.ceil(7 * scale);
        setMeasuredDimension(resolveSize(wantW, widthSpec), resolveSize(wantH, heightSpec));
    }

    @Override protected void onDraw(Canvas cv) {
        float x = gravity == Gravity.CENTER_HORIZONTAL ? (getWidth() - PixelFont.width(text, scale)) / 2f : 0;
        PixelFont.draw(cv, paint, x, 0, scale, text);
    }
}
