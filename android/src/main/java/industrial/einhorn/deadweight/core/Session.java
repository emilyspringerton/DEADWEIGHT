package industrial.einhorn.deadweight.core;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Client session state machine: CONNECTED -> READY -> QUEUED -> IN_MATCH -> READY ... A single reader thread drives
 * all inbound transitions and listener callbacks; callers (UI) must hop to their own thread. Outbound calls
 * (queue/play/leave) may come from any thread.
 */
public final class Session {
    public enum State { IDLE, CONNECTING, READY, QUEUED, IN_MATCH, CLOSED }

    public interface Listener {
        void onState(State s);
        void onQueued(int waiting);
        void onMatchFound(MatchModel m);
        void onRoundStart(MatchModel m);
        void onPlayAck(MatchModel m);
        void onPlayReject(MatchModel m, int reason);
        void onRoundResult(MatchModel m, MatchModel.RoundLog r);
        void onMatchEnd(MatchModel m);
        /** Terminal: connection lost or protocol/server error. State is CLOSED afterwards. */
        void onError(String message);
    }

    private final Transport transport;
    private final Listener listener;
    private final int mode, kind;
    private final String name;
    private final byte[] token;
    private volatile State state = State.IDLE;
    private volatile MatchModel match;
    private Thread reader;
    private volatile boolean autoQueue;
    // Android throws NetworkOnMainThreadException on socket writes from the UI thread, so every outbound frame goes
    // through this single writer thread (also keeps frame order).
    private final ExecutorService writer = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "dw-writer"); t.setDaemon(true); return t;
    });

    public Session(Transport t, Listener l, int mode, int kind, String name, byte[] token) {
        transport = t; listener = l; this.mode = mode; this.kind = kind; this.name = name; this.token = token;
    }

    /** One-tap play: queue automatically as soon as the server's WELCOME arrives (call before start()). */
    public void setAutoQueue(boolean v) { autoQueue = v; }

    public State state() { return state; }
    public MatchModel match() { return match; }

    private void set(State s) { state = s; listener.onState(s); }

    /** Connect + HELLO on a background thread; never throws. Progress via listener. */
    public synchronized void start() {
        if (reader != null) return;
        state = State.CONNECTING;
        reader = new Thread(this::run, "dw-session");
        reader.setDaemon(true);
        reader.start();
    }

    private void run() {
        try {
            listener.onState(State.CONNECTING);
            transport.connect();
            // Real IDUNA JWTs (~500 B) don't fit HELLO's 200 B field: HELLO carries none, AUTH carries it.
            transport.send(Protocol.hello(mode, kind, name, null));
            if (token != null && token.length > 0) transport.send(Protocol.auth(token));
            while (true) {
                byte[] f = transport.readFrame();
                if (f == null) { fail("server closed the connection"); return; }
                if (!handle(Protocol.decode(f))) return;
            }
        } catch (IOException | ProtocolException e) {
            if (state != State.CLOSED) fail(e.getMessage() == null ? e.toString() : e.getMessage());
        } catch (RuntimeException e) { // a listener bug must not kill the process silently
            fail("internal error: " + e);
        }
    }

    private boolean handle(Msg m) {
        switch (m.type) {
            case Protocol.S_WELCOME: set(State.READY); if (autoQueue) { autoQueue = false; queue(); } break;
            case Protocol.S_QUEUED: set(State.QUEUED); listener.onQueued(m.waiting); break;
            case Protocol.S_MATCH_FOUND: {
                MatchModel mm = new MatchModel(); mm.onMatchFound(m); match = mm;
                set(State.IN_MATCH); listener.onMatchFound(mm); break;
            }
            case Protocol.S_ROUND_START: if (match != null) { match.onRoundStart(m); listener.onRoundStart(match); } break;
            case Protocol.S_PLAY_ACK: if (match != null) { match.locked = true; listener.onPlayAck(match); } break;
            case Protocol.S_PLAY_REJECT: if (match != null) { match.lockedSlot = -2; listener.onPlayReject(match, m.reason); } break;
            case Protocol.S_ROUND_RESULT:
                if (match != null) { match.onRoundResult(m); listener.onRoundResult(match, match.log().get(match.log().size() - 1)); }
                break;
            case Protocol.S_MATCH_END:
                if (match != null) { match.onEnd(m); }
                set(State.READY); if (match != null) listener.onMatchEnd(match); break;
            case Protocol.S_PONG: break;
            case Protocol.S_ERROR: fail("server error code " + m.code); return false;
            default: fail("unexpected message 0x" + Integer.toHexString(m.type)); return false;
        }
        return true;
    }

    private void fail(String msg) {
        if (state == State.CLOSED) return;
        transport.close();
        writer.shutdown();
        state = State.CLOSED;
        listener.onState(State.CLOSED);
        listener.onError(msg);
    }

    private void send(byte[] f) {
        try {
            writer.execute(() -> {
                try { transport.send(f); }
                catch (IOException e) { fail("send failed: " + e.getMessage()); }
                catch (RuntimeException e) { fail("send failed: " + e); }
            });
        } catch (java.util.concurrent.RejectedExecutionException ignored) { }
    }

    public void queue() { if (state == State.READY) send(Protocol.queue()); }

    /** Lock a play (slot 0-3, or -1 = pass). Ignored (returns false) if the local legality gate says no. */
    public boolean play(int slot) {
        MatchModel mm = match;
        if (state != State.IN_MATCH || mm == null || !mm.canPlaySlot(slot)) return false;
        mm.lockedSlot = slot;
        send(Protocol.play(mm.matchId, mm.round, slot));
        return true;
    }

    public void leave() { if (state == State.QUEUED || state == State.IN_MATCH) send(Protocol.leave()); }

    public void close() {
        if (state == State.CLOSED) return;
        state = State.CLOSED;
        writer.shutdown();
        transport.close();
    }
}
