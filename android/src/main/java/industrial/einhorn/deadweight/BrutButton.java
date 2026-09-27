package industrial.einhorn.deadweight;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

/** A menu button, brutalist-rendered to match apps/gui/main.c's button(): a flat filled rect (no
 *  frame, no rounded corners, no ripple) with centered PixelFont text, dimmed to Theme.LOCK/DIM
 *  when disabled. Same "no host-OS widget chrome" rule CardView/BarView already apply to in-match
 *  UI, extended to the menu screen (docs/ANDROID_PARITY_NORTHSTAR.md menu-parity pass) -- replaces
 *  android.widget.Button, whose Material ripple/rounded corners/system font were the actual thing
 *  that made the old menu look like a different app from the Windows client. */
final class BrutButton extends View {
    private final Paint fill = new Paint();
    private final Paint text = new Paint();
    private String label = "";
    private int color = Theme.PANEL;
    private boolean enabled = true;
    private Runnable onClick;

    BrutButton(Context c) {
        super(c);
        setClickable(true);
        setOnClickListener(v -> { if (enabled && onClick != null) onClick.run(); });
    }

    /** color is the enabled fill (apps/gui/main.c passes a different Col per button -- orange for
     *  DRAFT, green for PRACTICE, blue for REDEEM/CLAIM, purple for FRIENDS); enabled false always
     *  renders Theme.LOCK regardless, matching button()'s own `Col f = enabled ? c : C_LOCK`. */
    void set(String label, int color, boolean enabled, Runnable onClick) {
        this.label = label; this.color = color; this.enabled = enabled; this.onClick = onClick;
        invalidate();
    }

    @Override protected void onDraw(Canvas cv) {
        float w = getWidth(), h = getHeight();
        fill.setColor(enabled ? color : Theme.LOCK);
        cv.drawRect(0, 0, w, h, fill);
        // button()'s own overflow fix: drop from scale 3 to 2 whenever the label wouldn't fit.
        float scale = PixelFont.width(label, 3) <= w - 8 ? 3 : 2;
        text.setColor(enabled ? Theme.TEXT : Theme.DIM);
        PixelFont.drawCentered(cv, text, w / 2, h / 2 - (scale == 3 ? 10 : 7), scale, label);
    }
}
