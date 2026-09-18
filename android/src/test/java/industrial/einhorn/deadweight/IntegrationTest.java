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
                public void onState(Session.State st) { if (st == Session.State.READY && h[0] != null && h[0].match() == null) h[0].queue(); }
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
            s.start();
            boolean ok = ended.await(20, TimeUnit.SECONDS);
            if (!ok || err[0] != null || result.get() < 0 || rounds.get() < 1) {
                fails++; System.out.println("FAIL ok=" + ok + " err=" + err[0] + " result=" + result.get() + " rounds=" + rounds.get());
            } else System.out.println("IntegrationTest: match complete, " + rounds.get() + " rounds, result " + result.get());
            s.close();
        } finally {
            if (bot != null) { bot.destroy(); bot.waitFor(2, TimeUnit.SECONDS); }
            server.destroy(); server.waitFor(2, TimeUnit.SECONDS);
        }
        System.out.println("IntegrationTest: " + fails + " failures");
        System.exit(fails == 0 ? 0 : 1);
    }
}
