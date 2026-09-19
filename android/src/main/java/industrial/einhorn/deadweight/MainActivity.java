package industrial.einhorn.deadweight;

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import industrial.einhorn.deadweight.core.GuestAuth;
import industrial.einhorn.deadweight.generated.CardRules;
import industrial.einhorn.deadweight.core.MatchModel;
import industrial.einhorn.deadweight.core.Protocol;
import industrial.einhorn.deadweight.core.Session;
import industrial.einhorn.deadweight.core.SocketTransport;
import java.nio.charset.StandardCharsets;

/**
 * Single-activity shell over the plain-JVM core. Screens are rebuilt from (session state, model) on every event, so
 * there is no per-screen state to lose. The manifest handles orientation/size changes itself (no activity recreation),
 * and the Session is closed in onDestroy. All listener callbacks arrive on the reader thread and hop to the UI thread.
 */
public final class MainActivity extends Activity implements Session.Listener {
    private static final String PREFS = "deadweight";
    private static final String[] REASONS = {"hull destroyed", "round limit", "opponent left", "server", "bankrupt"};

    private SharedPreferences prefs;
    private LinearLayout root;
    private Session session;
    private String status = "";          // connect/auth progress or error text shown on the menu
    private boolean connecting = false;
    private int selectedSlot = -2;        // -2 nothing chosen, -1 pass, 0-3 card
    private String lastRound = "";
    private boolean menuMode = true;      // true = show login/menu regardless of session state

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFF15171C);
        root.setPadding(24, 48, 24, 24);
        setContentView(root);
        render();
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        if (session != null) session.close();
    }

    // ---------------- Session.Listener (reader thread -> UI thread) ----------------
    @Override public void onState(Session.State s) { ui(this::render); }
    @Override public void onQueued(int w) { ui(this::render); }
    @Override public void onMatchFound(MatchModel m) { ui(() -> { selectedSlot = -2; lastRound = ""; menuMode = false; render(); }); }
    @Override public void onRoundStart(MatchModel m) { ui(() -> { selectedSlot = -2; render(); }); }
    @Override public void onPlayAck(MatchModel m) { ui(this::render); }
    @Override public void onPlayReject(MatchModel m, int r) { ui(() -> { selectedSlot = -2; status = "Play rejected (" + r + "), pick again"; render(); }); }
    @Override public void onRoundResult(MatchModel m, MatchModel.RoundLog r) {
        ui(() -> {
            lastRound = "Round " + r.round + ": you " + played(r.cardYou, r.effYou) + " vs " + played(r.cardOpp, r.effOpp)
                + "\nYou took " + r.dmgYou + (r.healYou > 0 ? " (healed " + r.healYou + ")" : "")
                + ", they took " + r.dmgOpp + (r.healOpp > 0 ? " (healed " + r.healOpp + ")" : "") + fxNotes(r);
            render();
        });
    }
    @Override public void onMatchEnd(MatchModel m) { ui(this::render); }
    @Override public void onError(String msg) { ui(() -> { status = "Disconnected: " + msg; connecting = false; menuMode = true; session = null; render(); }); }

    /** "Dark Pool -> Naked Short" when a card resolved as something else; names only otherwise. */
    private static String played(int card, int eff) {
        if (card < 0) return "PASS";
        return eff >= 0 && eff != card ? CardText.name(card) + " -> " + CardText.name(eff) : CardText.name(card);
    }

    private static String fxNotes(MatchModel.RoundLog r) {
        StringBuilder b = new StringBuilder();
        if ((r.flagsYou & 1) != 0) b.append("\nYour card was cancelled.");
        if ((r.flagsOpp & 1) != 0) b.append("\nTheir card was cancelled.");
        if ((r.flagsYou & 4) != 0) b.append("\nShieldbow saved you at 1 hull.");
        if ((r.flagsOpp & 4) != 0) b.append("\nTheir Shieldbow saved them at 1 hull.");
        if ((r.flagsYou & 16) != 0) b.append("\nHands were swapped!");
        if ((r.flagsYou & 8) != 0) b.append("\nYou locked one of their cards.");
        if ((r.flagsOpp & 8) != 0) b.append("\nOne of your cards is locked next round.");
        if ((r.flagsYou & 32) != 0) b.append("\nYou made them discard.");
        if ((r.flagsOpp & 32) != 0) b.append("\nThey made you discard.");
        return b.toString();
    }

    private static String meters(int armor, int vault, int status, boolean hidden) {
        String s = hidden ? "  A?  $?" : "  A" + armor + "  $" + vault;
        if ((status & 1) != 0) s += "  BURN";
        if ((status & 2) != 0) s += "  REGEN";
        if ((status & 4) != 0) s += "  HIDDEN";
        return s;
    }

    private void ui(Runnable r) { if (!isFinishing()) runOnUiThread(r); }

    // ---------------- rendering ----------------
    private void render() {
        root.removeAllViews();
        Session s = session;
        MatchModel m = s == null ? null : s.match();
        if (menuMode || s == null || s.state() == Session.State.CLOSED) { menuMode = true; renderMenu(); return; }
        switch (s.state()) {
            case IN_MATCH: renderMatch(s, m); break;
            case QUEUED: renderQueue(s); break;
            case READY: if (m != null && m.result >= 0) renderEnd(s, m); else renderLobby(s); break;
            default: text("Connecting…", 22, Color.WHITE); break;
        }
    }

    private TextView text(String t, int sp, int color) {
        TextView v = new TextView(this);
        v.setText(t); v.setTextSize(sp); v.setTextColor(color); v.setPadding(0, 8, 0, 8);
        root.addView(v, new LinearLayout.LayoutParams(-1, -2));
        return v;
    }

    private Button button(String label, View.OnClickListener l, LinearLayout parent, float weight) {
        Button b = new Button(this);
        b.setText(label); b.setTextSize(20); b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(weight > 0 ? 0 : -1, 150, weight);
        lp.setMargins(6, 6, 6, 6);
        (parent == null ? root : parent).addView(b, lp);
        return b;
    }

    private EditText field(String hint, String value, int inputType) {
        EditText e = new EditText(this);
        e.setHint(hint); e.setText(value); e.setInputType(inputType); e.setTextColor(Color.WHITE); e.setHintTextColor(0xFF888888);
        e.setSingleLine(true);
        root.addView(e, new LinearLayout.LayoutParams(-1, -2));
        return e;
    }

    private void renderMenu() {
        ScrollView sv = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        sv.addView(col);
        LinearLayout saved = root; // temporarily build into the scroll column
        root = col;
        text("DEADWEIGHT", 34, Color.WHITE).setTypeface(Typeface.DEFAULT_BOLD);
        text("Card duel, 1v1", 16, 0xFFAAAAAA);
        EditText name = field("Your name (1-16 chars)", prefs.getString("name", defaultName()), InputType.TYPE_CLASS_TEXT);
        EditText host = field("Server host", prefs.getString("host", Config.DEFAULT_HOST), InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        EditText port = field("Server port", String.valueOf(prefs.getInt("port", Config.DEFAULT_PORT)), InputType.TYPE_CLASS_NUMBER);
        EditText iduna = field("IDUNA URL (blank = name only)", prefs.getString("iduna", Config.DEFAULT_IDUNA_URL), InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        text(status, 15, 0xFFFFB74D);
        Button go = button(connecting ? "Connecting…" : "PLAY VS BOT", v -> {
            int p;
            try { p = Integer.parseInt(port.getText().toString().trim()); } catch (NumberFormatException e) { p = -1; }
            String n = name.getText().toString().trim();
            if (n.isEmpty() || n.length() > 16) { status = "Name must be 1-16 characters"; render(); return; }
            if (p < 1 || p > 65535) { status = "Bad port"; render(); return; }
            prefs.edit().putString("name", n).putString("host", host.getText().toString().trim()).putInt("port", p)
                .putString("iduna", iduna.getText().toString().trim()).apply();
            connect(n, host.getText().toString().trim(), p, iduna.getText().toString().trim());
        }, null, 0);
        go.setEnabled(!connecting);
        root = saved;
        root.addView(sv, new LinearLayout.LayoutParams(-1, -1));
    }

    private void connect(String name, String host, int port, String idunaUrl) {
        connecting = true; status = idunaUrl.isEmpty() ? "" : "Signing in…"; render();
        final String fn = name;
        new Thread(() -> {
            try {
                byte[] token = new byte[0];
                String shown = fn;
                if (!idunaUrl.isEmpty()) {
                    GuestAuth auth = new GuestAuth.Iduna(idunaUrl);
                    String pid = prefs.getString("player_id", ""), sec = prefs.getString("guest_secret", "");
                    GuestAuth.Result r = pid.isEmpty() ? auth.register(fn) : auth.login(pid, sec);
                    // Persist the identity; losing it loses the account (no recovery, by design).
                    prefs.edit().putString("player_id", r.playerId).putString("guest_secret", r.guestSecret).apply();
                    token = r.token.getBytes(StandardCharsets.UTF_8);
                    shown = r.displayName;
                }
                Session ns = new Session(new SocketTransport(host, port, 5000), this, Protocol.MODE_CARD, Protocol.KIND_HUMAN, shown, token);
                ns.setAutoQueue(true);
                ui(() -> { session = ns; connecting = false; status = ""; menuMode = false; ns.start(); render(); });
            } catch (Exception e) {
                ui(() -> { connecting = false; status = "Sign-in failed: " + e.getMessage(); render(); });
            }
        }, "dw-connect").start();
    }

    private String defaultName() { return "Player" + (1000 + new java.util.Random().nextInt(9000)); }

    private void renderLobby(Session s) {
        text("Connected", 26, Color.WHITE);
        text("Ready for a match.", 16, 0xFFAAAAAA);
        button("FIND MATCH", v -> s.queue(), null, 0);
        button("Disconnect", v -> disconnect(), null, 0);
    }

    private void renderQueue(Session s) {
        text("Searching for an opponent…", 24, Color.WHITE);
        text("A bot will take the seat if no human is waiting.", 14, 0xFFAAAAAA);
        button("Cancel", v -> { s.leave(); }, null, 0);
    }

    private void disconnect() { if (session != null) session.close(); session = null; menuMode = true; status = ""; render(); }

    private void renderEnd(Session s, MatchModel m) {
        String res = m.result == Protocol.RESULT_WIN ? "VICTORY" : m.result == Protocol.RESULT_LOSS ? "DEFEAT" : "DRAW";
        int col = m.result == Protocol.RESULT_WIN ? 0xFF43A967 : m.result == Protocol.RESULT_LOSS ? 0xFFD9534F : 0xFFFFD54F;
        text(res, 40, col).setGravity(Gravity.CENTER);
        text("vs " + m.oppName + "  |  " + m.hullYou + " - " + m.hullOpp + "  |  " + REASONS[Math.max(0, Math.min(4, m.endReason))], 16, 0xFFCCCCCC).setGravity(Gravity.CENTER);
        button("PLAY AGAIN", v -> { s.queue(); }, null, 0);
        button("Menu", v -> disconnect(), null, 0);
    }

    private void renderMatch(Session s, MatchModel m) {
        if (m == null) return;
        // Top: opponent + hull/energy
        BarView opp = new BarView(this);
        boolean oppHidden = m.energyOpp == Protocol.HIDDEN_U8;
        opp.set(m.oppName + " hull" + meters(m.armorOpp, m.vaultOpp, m.statusOpp, oppHidden), m.hullOpp, 20, oppHidden ? 0 : m.energyOpp, 0xFFD9534F);
        root.addView(opp, new LinearLayout.LayoutParams(-1, 130));
        BarView me = new BarView(this);
        me.set("You hull" + meters(m.armorYou, m.vaultYou, m.statusYou, false), m.hullYou, 20, m.energyYou, 0xFF43A967);
        root.addView(me, new LinearLayout.LayoutParams(-1, 130));
        text("Round " + m.round + " / " + CardRules.maxRounds() + "   (opp hand: " + m.oppHandSize + ")", 16, 0xFFAAAAAA);
        // Middle: last round reveal (grows to fill)
        String hint = selectedSlot >= 0 && m.hand[selectedSlot] >= 0
            ? CardText.name(m.hand[selectedSlot]) + ": " + CardText.text(m.hand[selectedSlot]) : "";
        TextView log = text(lastRound.isEmpty() ? "Pick a card and lock in." : lastRound, 18, Color.WHITE);
        log.setGravity(Gravity.CENTER);
        ((LinearLayout.LayoutParams) log.getLayoutParams()).height = 0;
        ((LinearLayout.LayoutParams) log.getLayoutParams()).weight = 1;
        if (!hint.isEmpty() && !m.locked) text(hint, 16, 0xFF9AD0FF).setGravity(Gravity.CENTER);
        if (m.locked) text("Locked in. Waiting for opponent…", 16, 0xFFFFD54F).setGravity(Gravity.CENTER);
        else if (!status.isEmpty()) text(status, 14, 0xFFFFB74D).setGravity(Gravity.CENTER);
        // Thumb zone (bottom): action row, then the hand
        LinearLayout actions = new LinearLayout(this);
        root.addView(actions, new LinearLayout.LayoutParams(-1, -2));
        Button pass = button(selectedSlot == -1 ? "PASS (selected)" : "PASS  +1 energy", v -> { selectedSlot = -1; render(); }, actions, 1);
        pass.setEnabled(!m.locked);
        Button lock = button("LOCK IN", v -> { status = ""; if (selectedSlot != -2 && s.play(selectedSlot)) render(); }, actions, 1);
        lock.setEnabled(!m.locked && selectedSlot != -2);
        LinearLayout hand = new LinearLayout(this);
        hand.setWeightSum(4);
        root.addView(hand, new LinearLayout.LayoutParams(-1, 360));
        for (int i = 0; i < 4; i++) {
            final int slot = i;
            CardView cv = new CardView(this);
            cv.set(m.hand[i], m.canPlaySlot(i), selectedSlot == i, ((m.lockMask >> i) & 1) != 0);
            cv.setOnClickListener(v -> { if (m.canPlaySlot(slot)) { selectedSlot = slot; render(); } });
            hand.addView(cv, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        }
    }
}
