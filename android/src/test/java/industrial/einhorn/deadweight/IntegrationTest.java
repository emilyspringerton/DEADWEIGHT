package industrial.einhorn.deadweight;

import industrial.einhorn.deadweight.core.*;
import java.io.File;
import java.net.ServerSocket;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Plays a full match through Session against a real dw_server --fast-forward --no-auth plus one real dw_bot.
 * Usage: IntegrationTest <dw_server> <dw_bot>  (skips cleanly, exit 0, if either binary is absent).
 * Cleanup kills only the exact child processes this test spawned.
 */
public final class IntegrationTest {
    public static void main(String[] a) throws Exception {
        if (a.length < 2 || !new File(a[0]).canExecute() || !new File(a[1]).canExecute()) {
            System.out.println("IntegrationTest: SKIP (dw_server/dw_bot not built)");
            return;
        }
        int port; try (ServerSocket ss = new ServerSocket(0)) { port = ss.getLocalPort(); }
        Process server = new ProcessBuilder(a[0], "--fast-forward", "--no-auth", "--port", String.valueOf(port))
            .redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start();
        Process bot = null;
        int fails = 0;
        try {
            for (int i = 0; i < 50; i++) { // wait for the listener
                try (java.net.Socket s = new java.net.Socket("127.0.0.1", port)) { break; } catch (Exception e) { Thread.sleep(100); }
            }
            bot = new ProcessBuilder(a[1], "--archetype", "ripper", "--name", "itbot", "--port", String.valueOf(port), "--matches", "1")
                .redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start();
            CountDownLatch ended = new CountDownLatch(1);
            AtomicInteger rounds = new AtomicInteger(), result = new AtomicInteger(-1);
            final Session[] h = new Session[1];
            String[] err = {null};
            Session s = new Session(new SocketTransport("127.0.0.1", port, 3000), new Session.Listener() {
                public void onState(Session.State st) { }
                public void onQueued(int w) { }
                public void onMatchFound(MatchModel m) { }
                public void onRoundStart(MatchModel m) {
                    rounds.incrementAndGet();
                    int slot = -1;
                    for (int i = 0; i < 4; i++) if (m.canPlaySlot(i)) { slot = i; break; }
                    h[0].play(slot);
                }
                public void onPlayAck(MatchModel m) { }
                public void onPlayReject(MatchModel m, int r) { err[0] = "reject " + r; }
                public void onRoundResult(MatchModel m, MatchModel.RoundLog r) { }
                public void onMatchEnd(MatchModel m) { result.set(m.result); ended.countDown(); }
                public void onError(String msg) { err[0] = msg; ended.countDown(); }
            }, Protocol.MODE_CARD, Protocol.KIND_HUMAN, "itHuman", new byte[0]);
            h[0] = s;
            s.setAutoQueue(true); // exactly what the app's one-tap PLAY does: no explicit queue() call
            s.start();
            boolean ok = ended.await(20, TimeUnit.SECONDS);
            if (!ok || err[0] != null || result.get() < 0 || rounds.get() < 1) {
                fails++; System.out.println("FAIL ok=" + ok + " err=" + err[0] + " result=" + result.get() + " rounds=" + rounds.get());
            } else System.out.println("IntegrationTest: match complete, " + rounds.get() + " rounds, result " + result.get());
            s.close();
            fails += draftLeg(a, port);
        } finally {
            if (bot != null) { bot.destroy(); bot.waitFor(2, TimeUnit.SECONDS); }
            server.destroy(); server.waitFor(2, TimeUnit.SECONDS);
        }
        System.out.println("IntegrationTest: " + fails + " failures");
        System.exit(fails == 0 ? 0 : 1);
    }

    /** Draft leg: a real drafting Session (16 picks through Session.pick) against a real draft-queue dw_bot, then a same-deck replay. */
    static int draftLeg(String[] a, int port) throws Exception {
        Process bot = new ProcessBuilder(a[1], "--archetype", "wall", "--mode", "draft", "--name", "itbot-d", "--port", String.valueOf(port))
            .redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start();
        int fails = 0;
        try {
            CountDownLatch ended = new CountDownLatch(2);
            AtomicInteger picks = new AtomicInteger(), matches = new AtomicInteger(), doneCards = new AtomicInteger(-1);
            String[] err = {null};
            final Session[] h = new Session[1];
            final int[] firstDeck = {0};
            Session s = new Session(new SocketTransport("127.0.0.1", port, 3000), new Session.Listener() {
                public void onState(Session.State st) { }
                public void onQueued(int w) { }
                public void onMatchFound(MatchModel m) { }
                public void onRoundStart(MatchModel m) {
                    int slot = -1;
                    for (int i = 0; i < 4; i++) if (m.canPlaySlot(i)) { slot = i; break; }
                    h[0].play(slot);
                }
                public void onPlayAck(MatchModel m) { }
                public void onPlayReject(MatchModel m, int r) { err[0] = "reject " + r; }
                public void onRoundResult(MatchModel m, MatchModel.RoundLog r) { }
                public void onDraftOffer(DraftModel d) {
                    int m = 1; while (m < 3 && !d.canPick(m)) m++;
                    if (h[0].pick(0, m)) picks.incrementAndGet();
                }
                public void onDraftDone(int id, int[] deck) { doneCards.set(deck.length); firstDeck[0] = deck[0]; }
                public void onMatchEnd(MatchModel m) {
                    matches.incrementAndGet(); ended.countDown();
                    if (matches.get() == 1) new Thread(() -> h[0].queue(true), "requeue").start(); // replay the same deck
                }
                public void onError(String msg) { err[0] = msg; ended.countDown(); ended.countDown(); }
            }, Protocol.MODE_DRAFT, Protocol.KIND_HUMAN, "itDraft", new byte[0]);
            h[0] = s;
            s.setAutoQueue(true);
            s.start();
            boolean ok = ended.await(30, TimeUnit.SECONDS);
            int[] deck = s.deck();
            if (!ok || err[0] != null || picks.get() != Protocol.DRAFT_PICKS || doneCards.get() != Protocol.DRAFT_DECK || deck == null || deck.length != Protocol.DRAFT_DECK
                || s.draft().picks().size() != Protocol.DRAFT_PICKS || matches.get() != 2) {
                fails++; System.out.println("FAIL draft ok=" + ok + " err=" + err[0] + " picks=" + picks.get() + " done=" + doneCards.get() + " matches=" + matches.get());
            } else {
                int total = 0; for (DraftModel.Pick p : s.draft().picks()) total += p.mult;
                if (total != Protocol.DRAFT_DECK) { fails++; System.out.println("FAIL draft picks sum to " + total); }
                else System.out.println("IntegrationTest: draft complete, 16 picks -> 23-card deck, 2 matches (second on the same deck)");
            }
            s.close();
        } finally { bot.destroy(); bot.waitFor(2, TimeUnit.SECONDS); }
        return fails;
    }
}
