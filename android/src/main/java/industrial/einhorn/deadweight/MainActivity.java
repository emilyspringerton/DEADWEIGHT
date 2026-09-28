package industrial.einhorn.deadweight;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import industrial.einhorn.deadweight.core.DraftModel;
import industrial.einhorn.deadweight.core.FxTimeline;
import industrial.einhorn.deadweight.core.GuestAuth;
import industrial.einhorn.deadweight.generated.AccountRulesScalar;
import industrial.einhorn.deadweight.generated.CardRules;
import industrial.einhorn.deadweight.core.MatchModel;
import industrial.einhorn.deadweight.core.Protocol;
import industrial.einhorn.deadweight.core.Session;
import industrial.einhorn.deadweight.core.SocketTransport;
import java.io.IOException;
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
    // Round-reveal animation (S555 phase 2): the last ROUND_RESULT, its PARENA-decided timeline, and when it arrived
    // (uptime ms) so screen rebuilds mid-animation don't restart it.
    private MatchModel.RoundLog fxRound;
    private FxTimeline fxTimeline;
    private long fxStartMs;
    private boolean menuMode = true;      // true = show login/menu regardless of session state

    // Zero-friction IDUNA auth (docs/ANDROID_PARITY_NORTHSTAR.md menu-parity pass, porting
    // apps/gui/main.c's S508 iduna_bootstrap() -- see bootstrapAuth()). Runs once at launch, not
    // per-play: the player never sees a name/host/port/IDUNA-URL field, matching the desktop
    // client's own S512 "zero-friction auth" UX exactly.
    private GuestAuth.Iduna iduna;
    private boolean authBooting = true;   // true until the boot-time login/register attempt finishes
    private boolean authReady = false;
    private String authErr = "";
    private String authToken = "", authName = "";
    private int tickets = 0;
    private boolean isGuest = true, isFounder = false;   // isFounder is session-local only, matching A.is_founder
    private String redeemMsg = "";
    private int waitingCount = 0;   // apps/gui/main.c's A.waiting -- how many players S_QUEUED reported waiting

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Theme.BG);
        root.setPadding(24, 48, 24, 24);
        setContentView(root);
        render();
        bootstrapAuth();
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        if (session != null) session.close();
    }

    // ---------------- Session.Listener (reader thread -> UI thread) ----------------
    @Override public void onState(Session.State s) { ui(this::render); }
    @Override public void onQueued(int w) { ui(() -> { waitingCount = w; render(); }); }
    @Override public void onMatchFound(MatchModel m) { ui(() -> { selectedSlot = -2; lastRound = ""; fxRound = null; fxTimeline = null; menuMode = false; render(); }); }
    @Override public void onRoundStart(MatchModel m) { ui(() -> { selectedSlot = -2; render(); }); }
    @Override public void onPlayAck(MatchModel m) { ui(this::render); }
    @Override public void onPlayReject(MatchModel m, int r) { ui(() -> { selectedSlot = -2; status = "Play rejected (" + r + "), pick again"; render(); }); }
    @Override public void onRoundResult(MatchModel m, MatchModel.RoundLog r) {
        ui(() -> {
            lastRound = "Round " + r.round + ": you " + played(r.cardYou, r.effYou) + " vs " + played(r.cardOpp, r.effOpp)
                + "\nYou took " + r.dmgYou + (r.healYou > 0 ? " (healed " + r.healYou + ")" : "")
                + ", they took " + r.dmgOpp + (r.healOpp > 0 ? " (healed " + r.healOpp + ")" : "") + fxNotes(r);
            fxRound = r; fxTimeline = FxTimeline.of(r); fxStartMs = SystemClock.uptimeMillis();
            render();
        });
    }
    @Override public void onMatchEnd(MatchModel m) {
        // Like the desktop client, the final round's reveal finishes before the result screen replaces it.
        ui(() -> { render(); long left = fxRemainingMs(); if (left > 0) root.postDelayed(this::render, left + 50); });
    }

    private long fxRemainingMs() {
        return fxTimeline == null ? 0 : fxStartMs + fxTimeline.totalMs - SystemClock.uptimeMillis();
    }
    @Override public void onDraftOffer(DraftModel d) { ui(this::render); }
    @Override public void onDraftDone(int deckId, int[] deck) { ui(this::render); }
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
        if ((r.flagsYou & 4) != 0) b.append("\nAegisbow saved you at 1 hull.");
        if ((r.flagsOpp & 4) != 0) b.append("\nTheir Aegisbow saved them at 1 hull.");
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
            case DRAFTING: renderDraft(s); break;
            case QUEUED: renderQueue(s); break;
            case READY:
                if (m != null && m.result >= 0 && fxRemainingMs() > 0) renderMatch(s, m);
                else if (m != null && m.result >= 0) renderEnd(s, m);
                else renderLobby(s);
                break;
            default: text("Connecting…", 22, Theme.TEXT); break;
        }
    }

    private TextView text(String t, int sp, int color) {
        TextView v = new TextView(this);
        v.setText(t); v.setTextSize(sp); v.setTextColor(color); v.setPadding(0, 8, 0, 8);
        root.addView(v, new LinearLayout.LayoutParams(-1, -2));
        return v;
    }

    private Button button(String label, View.OnClickListener l, LinearLayout parent, float weight) { return button(label, l, parent, weight, 150); }

    private Button button(String label, View.OnClickListener l, LinearLayout parent, float weight, int heightPx) {
        Button b = new Button(this);
        b.setText(label); b.setTextSize(20); b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(weight > 0 ? 0 : -1, heightPx, weight);
        lp.setMargins(6, 6, 6, 6);
        (parent == null ? root : parent).addView(b, lp);
        return b;
    }

    // ---------------- IDUNA zero-friction auth (S508/S512 parity, apps/gui/main.c iduna_bootstrap) ----------------

    /** Runs once at launch, not per-play. Mirrors iduna_bootstrap()'s real shape exactly, including
     *  its one genuinely surprising behavior: ANY login failure -- network error or a rejected
     *  saved secret alike -- falls through to registering a brand-new guest, same as the C client's
     *  own unconditional `if (!ok) { ...register... }`. That's a real, accepted risk on the desktop
     *  client already (a flaky network on launch can abandon a saved account), not something to
     *  "fix" while porting -- matching it exactly is the actual parity target. */
    private void bootstrapAuth() {
        if (Config.DEFAULT_IDUNA_URL.isEmpty()) { authBooting = false; render(); return; }
        iduna = new GuestAuth.Iduna(Config.DEFAULT_IDUNA_URL);
        authBooting = true; render();
        new Thread(() -> {
            String pid = prefs.getString("player_id", ""), sec = prefs.getString("guest_secret", "");
            GuestAuth.Result r = null;
            if (!pid.isEmpty() && !sec.isEmpty()) {
                try { r = iduna.login(pid, sec); } catch (IOException e) { /* fall through to register */ }
            }
            String err = "";
            if (r == null) {
                try {
                    // S512 zero-friction auth: an empty display_name is the real, expected path --
                    // IDUNA auto-assigns a lore-friendly one ("Runner-A7B2"), same as the desktop
                    // client's own wantname="" default (a --name dev override has no Android
                    // equivalent and isn't needed -- this app has no CLI).
                    r = iduna.register("");
                    prefs.edit().putString("player_id", r.playerId).putString("guest_secret", r.guestSecret).apply();
                } catch (IOException e) {
                    err = "IDUNA unreachable -- playing without an account (no tickets)";
                }
            }
            final GuestAuth.Result rr = r; final String ferr = err;
            ui(() -> {
                authBooting = false;
                if (rr != null) {
                    authToken = rr.token; authName = rr.displayName; tickets = rr.tickets; isGuest = rr.isGuest;
                    authReady = true;
                } else authErr = ferr;
                render();
            });
        }, "dw-auth-bootstrap").start();
    }

    private void doRedeem(String code) {
        if (code.isEmpty()) return;
        if (!authReady) { redeemMsg = "No account (IDUNA offline)"; render(); return; }
        new Thread(() -> {
            try {
                GuestAuth.RedeemResult r = iduna.redeem(authToken, code);
                ui(() -> {
                    tickets = r.balance; if (r.founder) isFounder = true;
                    redeemMsg = "+" + r.ticketsGranted + " ticket" + (r.ticketsGranted == 1 ? "" : "s") + (r.founder ? " + FOUNDER" : "");
                    render();
                });
            } catch (IOException e) {
                ui(() -> { redeemMsg = "Invalid or already-used code"; render(); });
            }
        }, "dw-redeem").start();
    }

    /** "SECURE CONNECTION (CLAIM ACCOUNT)" -- a native AlertDialog rather than a fully custom
     *  brutalist modal (apps/gui/main.c's own S_CLAIM screen): a real, honest simplification named
     *  in docs/ANDROID_PARITY_NORTHSTAR.md, not hidden -- two lines of real text entry don't
     *  justify reimplementing raw touch-keyboard capture, and the fields are still theme-colored. */
    private void showClaimDialog() {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(48, 16, 48, 0);
        EditText email = brutField("EMAIL", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        EditText pass = brutField("PASSWORD", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        col.addView(email); col.addView(pass);
        new AlertDialog.Builder(this)
            .setTitle("SECURE CONNECTION")
            .setMessage("Link email -- keep your progress")
            .setView(col)
            .setPositiveButton("SUBMIT", (d, w) -> doClaim(email.getText().toString().trim(), pass.getText().toString()))
            .setNegativeButton("CANCEL", null)
            .show();
    }

    private EditText brutField(String hint, int inputType) {
        EditText e = new EditText(this);
        e.setHint(hint); e.setInputType(inputType); e.setSingleLine(true);
        e.setTextColor(Theme.TEXT); e.setHintTextColor(Theme.DIM); e.setTypeface(Typeface.MONOSPACE);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Theme.BG); bg.setStroke(2, Theme.LOCK);
        e.setBackground(bg);
        e.setPadding(20, 16, 20, 16);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = 16;
        e.setLayoutParams(lp);
        return e;
    }

    /** Mirrors do_link_email exactly, including its 409-is-not-a-failure login-fallback: this
     *  email already belongs to a DIFFERENT player_id, so the real, intended response is signing
     *  into THAT account instead, not surfacing an error. */
    private void doClaim(String email, String pass) {
        if (email.isEmpty() || pass.isEmpty()) return;
        if (!authReady) { redeemMsg = "No account (IDUNA offline)"; render(); return; }
        if (!AccountRulesScalar.isValidPasswordLength(pass.length())) {
            redeemMsg = "Password needs " + AccountRulesScalar.minPasswordLen() + "+ characters"; render(); return;
        }
        new Thread(() -> {
            String newTok = null, err = null; boolean loggedInInstead = false;
            try { newTok = iduna.upgrade(authToken, email, pass); }
            catch (GuestAuth.EmailTaken e) {
                try { newTok = iduna.emailLogin(email, pass); loggedInInstead = true; }
                catch (IOException e2) { err = "Link failed (email taken or bad login)"; }
            } catch (IOException e) { err = "Link failed (email taken or bad login)"; }
            final String tok = newTok, ferr = err; final boolean instead = loggedInInstead;
            ui(() -> {
                if (tok != null) {
                    authToken = tok; isGuest = false;
                    redeemMsg = instead ? "This email already had an account -- signed in to it instead." : "Linked! Progress now saved.";
                } else redeemMsg = ferr;
                render();
            });
        }, "dw-claim").start();
    }

    // ---------------- menu (brutalist parity pass -- docs/ANDROID_PARITY_NORTHSTAR.md) ----------------

    private PixelLabel label(String t, float scale, int color) { return label(t, scale, color, root, Gravity.CENTER_HORIZONTAL); }

    /** Variant for a non-root parent (e.g. a draft column) and a caller-chosen gravity, matching
     *  apps/gui/main.c's text() (left) vs text_c() (centered) split at each individual call site. */
    private PixelLabel label(String t, float scale, int color, LinearLayout parent, int gravity) {
        PixelLabel v = new PixelLabel(this, t, scale, color);
        v.setGravity(gravity);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = (int) (6 * scale);
        parent.addView(v, lp);
        return v;
    }

    private BrutButton brutButton(String label, int color, boolean enabled, Runnable onClick) {
        return brutButton(label, color, enabled, onClick, root, 130);
    }

    /** Variant for a non-root parent (e.g. a draft column) and a caller-chosen height, matching
     *  apps/gui/main.c's button() taking an explicit w/h per call site rather than one fixed size. */
    private BrutButton brutButton(String label, int color, boolean enabled, Runnable onClick, LinearLayout parent, int heightPx) {
        BrutButton b = new BrutButton(this);
        b.set(label, color, enabled, onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, heightPx);
        lp.topMargin = 18;
        parent.addView(b, lp);
        return b;
    }

    private void renderMenu() {
        label("DEADWEIGHT", 5, Theme.TEXT);
        if (authBooting) {
            label("ESTABLISHING CONNECTION...", 2, Theme.DIM);
            return;
        }
        if (authReady) {
            label(authName, 3, Theme.TEXT);
            label("TICKETS: " + tickets, 2, Theme.DIM);
            if (isFounder) label("FOUNDER", 1, Theme.FOUNDER_GOLD);
        } else {
            label(authErr.isEmpty() ? "NO ACCOUNT" : authErr, 2, Theme.DIM);
        }
        boolean canDraft = tickets > 0;
        brutButton(connecting ? "CONNECTING..." : canDraft ? "DRAFT  (COST: 1 TICKET)" : "DRAFT  (NO TICKETS)",
            Theme.KIND_COLOR[1], !connecting && canDraft, () -> connect(Protocol.MODE_DRAFT));
        brutButton(connecting ? "CONNECTING..." : "PRACTICE  (RANDOM DECK, FREE)", Theme.GOOD, !connecting, () -> connect(Protocol.MODE_CARD));

        label("REDEEM CODE", 2, Theme.DIM);
        EditText redeem = brutField("CODE", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS);
        root.addView(redeem);   // uses the LayoutParams brutField() already set (topMargin included)
        brutButton("REDEEM", Theme.BLUE, true, () -> doRedeem(redeem.getText().toString().trim()));

        if (authReady && isGuest) {
            brutButton("SECURE CONNECTION (CLAIM ACCOUNT)", Theme.BLUE, true, this::showClaimDialog);
        } else if (authReady) {
            label("CONNECTION SECURED", 2, Theme.GOOD);
        }
        // Same priority order as apps/gui/main.c's draw_menu(): link/redeem confirmations first
        // (Theme.GOOD, even though a redeem failure message reuses this field -- matches Windows'
        // own single-color-for-both-outcomes choice), then a connection failure.
        if (!redeemMsg.isEmpty()) label(redeemMsg, 1, Theme.GOOD);
        else if (!status.isEmpty()) label(status, 1, Theme.BAD);
        label("V" + versionName(), 1, Theme.DIM);
    }

    private void connect(int mode) {
        connecting = true; status = ""; render();
        final String name = authReady ? authName : ("Player" + (1000 + new java.util.Random().nextInt(9000)));
        final byte[] token = authReady ? authToken.getBytes(StandardCharsets.UTF_8) : new byte[0];
        new Thread(() -> {
            try {
                Session ns = new Session(new SocketTransport(Config.DEFAULT_HOST, Config.DEFAULT_PORT, 5000), this, mode, Protocol.KIND_HUMAN, name, token);
                ns.setAutoQueue(true);
                ui(() -> { session = ns; connecting = false; status = ""; menuMode = false; ns.start(); render(); });
            } catch (Exception e) {
                ui(() -> { connecting = false; status = "Connection failed: " + e.getMessage(); render(); });
            }
        }, "dw-connect").start();
    }

    /** apps/gui/main.c's own V%s footer reads a build-time DW_VERSION macro; the closest Android
     *  equivalent is the manifest's real versionName (android:versionName, AndroidManifest.xml). */
    private String versionName() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (Exception e) { return "?"; }
    }

    // apps/gui/main.c has no separate "connected, click Find Match" screen -- autoQueue means
    // this state is normally passed straight through -- but it's a real, reachable state (e.g. a
    // draft-mode requeue that doesn't auto-fire), so it gets the same brutalist treatment as
    // everything else rather than staying the one screen still on stock widgets.
    private void renderLobby(Session s) {
        label("DEADWEIGHT", 5, Theme.TEXT);
        label("CONNECTED", 2, Theme.GOOD);
        brutButton("FIND MATCH", Theme.GOOD, true, s::queue);
        brutButton("DISCONNECT", Theme.BAD, true, this::disconnect);
    }

    // Matches apps/gui/main.c's draw_queue(): big centered "SEARCHING...", waiting count once
    // welcomed (S_QUEUED's own waiting field, apps/gui/main.c's A.waiting), the player's own
    // name, and a single CANCEL button -- same content, same layout, brutalist chrome.
    private void renderQueue(Session s) {
        label("SEARCHING...", 4, Theme.TEXT);
        label(s.state() == Session.State.QUEUED ? "IN QUEUE  (" + waitingCount + " WAITING)" : "CONNECTING...", 2, Theme.DIM);
        label("PLAYING AS " + (authReady ? authName : "?"), 2, Theme.DIM);
        if (s.isDraft() && s.deck() != null) label("DECK " + s.deckId() + " LOCKED IN (" + s.deck().length + " CARDS)", 1, Theme.BLUE);
        brutButton("CANCEL", Theme.BAD, true, s::leave);
    }

    private void disconnect() { if (session != null) session.close(); session = null; menuMode = true; status = ""; render(); }

    // Matches apps/gui/main.c's draw_end(): big centered VICTORY/DEFEAT/DRAW in the outcome's own
    // color, the end reason, the hull comparison, then either SAME DECK/REDRAFT (draft mode) or
    // PLAY AGAIN (card mode), then MENU. Real, named simplification kept from before this pass
    // (not something this pass changed): the desktop client routes a draft-mode match end through
    // its own S_DRAFT_HUB screen first (win-streak "burned proxies" tracker, RESUME UPLINK /
    // ABORT & EXTRACT) -- Android has no Draft Hub screen or win/loss-streak state at all and
    // goes straight from match-end to SAME DECK/REDRAFT, same as before this pass. Building the
    // Hub is new stateful feature work (a whole screen plus win/loss tracking), not a UI reskin,
    // so it stays a named, deferred gap rather than a half-built stand-in.
    private void renderEnd(Session s, MatchModel m) {
        String res = m.result == Protocol.RESULT_WIN ? "VICTORY" : m.result == Protocol.RESULT_LOSS ? "DEFEAT" : "DRAW";
        int col = m.result == Protocol.RESULT_WIN ? Theme.GOOD : m.result == Protocol.RESULT_LOSS ? Theme.BAD : Theme.DIM;
        label(res, 6, col);
        label(REASONS[Math.max(0, Math.min(4, m.endReason))].toUpperCase(java.util.Locale.ROOT), 2, Theme.DIM);
        label("YOU " + Math.max(0, m.hullYou) + "  -  " + m.oppName + " " + Math.max(0, m.hullOpp), 2, Theme.TEXT);
        if (s.isDraft()) {
            brutButton("SAME DECK", Theme.GOOD, true, () -> s.queue(true));
            brutButton("REDRAFT", Theme.GOOD, true, () -> s.queue(false));
        } else {
            brutButton("PLAY AGAIN", Theme.GOOD, true, s::queue);
        }
        brutButton("MENU", Theme.LOCK, true, this::disconnect);
    }

    // Matches apps/gui/main.c's draw_draft(): pick counter, copies-left tally, two offered cards
    // (CardView already brutalist since Phase 1) each with its own 1x/2x/3x pick buttons, then the
    // deck-so-far list, then LEAVE -- same content and order, brutalist chrome throughout (the
    // 1x/2x/3x/Leave buttons were the one part of this screen still on stock android.widget.Button).
    private void renderDraft(Session s) {
        DraftModel d = s.draft();
        label("DRAFT  PICK " + Math.min(d.pickNo + 1, d.total) + "/" + d.total, 3, Theme.TEXT);
        label("LEFT  1X: " + d.left[0] + "   2X: " + d.left[1] + "   3X: " + d.left[2], 2, Theme.DIM);
        LinearLayout row = new LinearLayout(this);
        row.setWeightSum(2);
        root.addView(row, new LinearLayout.LayoutParams(-1, 0, 1));
        for (int c = 0; c < 2; c++) {
            final int idx = c;
            LinearLayout col = new LinearLayout(this);
            col.setOrientation(LinearLayout.VERTICAL);
            row.addView(col, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
            CardView cv = new CardView(this);
            cv.set(d.offer[c], true, false, false);
            col.addView(cv, new LinearLayout.LayoutParams(-1, 0, 1));
            LinearLayout mults = new LinearLayout(this);
            col.addView(mults, new LinearLayout.LayoutParams(-1, -2));
            for (int m = 1; m <= 3; m++) {
                final int mult = m;
                brutButton(m + "X", Theme.GOOD, d.canPick(m), () -> s.pick(idx, mult), mults, 130);
            }
        }
        label("YOUR DECK SO FAR (" + d.picks().size() + "/" + d.total + ")", 2, Theme.DIM, root, Gravity.START);
        LinearLayout deckCol = new LinearLayout(this);
        deckCol.setOrientation(LinearLayout.VERTICAL);
        for (DraftModel.Pick p : d.picks()) label(p.mult + "X " + CardText.name(p.card), 2, Theme.TEXT, deckCol, Gravity.START);
        ScrollView sv = new ScrollView(this);
        sv.addView(deckCol);
        root.addView(sv, new LinearLayout.LayoutParams(-1, 240));
        brutButton("LEAVE", Theme.LOCK, true, () -> { s.leave(); disconnect(); }, root, 110);
    }

    private void renderMatch(Session s, MatchModel m) {
        if (m == null) return;
        // Top: opponent + hull/energy
        BarView opp = new BarView(this);
        boolean oppHidden = m.energyOpp == Protocol.HIDDEN_U8;
        opp.set(m.oppName + " hull" + meters(m.armorOpp, m.vaultOpp, m.statusOpp, oppHidden), m.hullOpp, 20, oppHidden ? 0 : m.energyOpp, Theme.BAD);
        root.addView(opp, new LinearLayout.LayoutParams(-1, 130));
        BarView me = new BarView(this);
        me.set("You hull" + meters(m.armorYou, m.vaultYou, m.statusYou, false), m.hullYou, 20, m.energyYou, Theme.GOOD);
        root.addView(me, new LinearLayout.LayoutParams(-1, 130));
        text("Round " + m.round + " / " + CardRules.maxRounds() + "   (opp hand: " + m.oppHandSize + ")", 16, Theme.DIM);
        // Middle: last round reveal (grows to fill)
        String hint = selectedSlot >= 0 && m.hand[selectedSlot] >= 0
            ? CardText.name(m.hand[selectedSlot]) + ": " + CardText.text(m.hand[selectedSlot]) : "";
        if (fxRound != null) {
            FxView arena = new FxView(this);
            arena.set(fxRound, fxTimeline, fxStartMs);
            root.addView(arena, new LinearLayout.LayoutParams(-1, 0, 1));
            text(lastRound, 14, Theme.DIM).setGravity(Gravity.CENTER);
        } else {
            TextView log = text("Pick a card and lock in.", 18, Theme.TEXT);
            log.setGravity(Gravity.CENTER);
            ((LinearLayout.LayoutParams) log.getLayoutParams()).height = 0;
            ((LinearLayout.LayoutParams) log.getLayoutParams()).weight = 1;
        }
        if (!hint.isEmpty() && !m.locked) text(hint, 16, 0xFF468CE6).setGravity(Gravity.CENTER);
        if (m.locked) text("Locked in. Waiting for opponent…", 16, 0xFFFAD246).setGravity(Gravity.CENTER);
        else if (!status.isEmpty()) text(status, 14, 0xFFFAD246).setGravity(Gravity.CENTER);
        // Thumb zone (bottom): action row, then the hand
        LinearLayout actions = new LinearLayout(this);
        root.addView(actions, new LinearLayout.LayoutParams(-1, -2));
        Button pass = button(selectedSlot == -1 ? "PASS (selected)" : "PASS  +1 energy", v -> { selectedSlot = -1; render(); }, actions, 1);
        boolean live = s.state() == Session.State.IN_MATCH; // false while the final round's reveal finishes
        pass.setEnabled(!m.locked && live);
        Button lock = button("LOCK IN", v -> { status = ""; if (selectedSlot != -2 && s.play(selectedSlot)) render(); }, actions, 1);
        lock.setEnabled(!m.locked && live && selectedSlot != -2);
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
