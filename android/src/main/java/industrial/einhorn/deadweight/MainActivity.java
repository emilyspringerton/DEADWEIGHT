package industrial.einhorn.deadweight;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import industrial.einhorn.deadweight.generated.CardRules;

/** Skeleton shell (S503-02): proves the PARENA-generated rules run inside the APK. Real UI lands in S503-07. */
public final class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView t = new TextView(this);
        t.setPadding(48, 96, 48, 48);
        t.setTextSize(18f);
        t.setText("DEADWEIGHT\ncard rules loaded: " + CardRules.numCards() + " cards, hull "
            + CardRules.startHull() + "\n(VS0 skeleton)");
        setContentView(t);
    }
}
