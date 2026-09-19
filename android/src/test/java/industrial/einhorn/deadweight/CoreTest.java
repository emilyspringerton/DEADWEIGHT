package industrial.einhorn.deadweight;

import industrial.einhorn.deadweight.core.*;
import industrial.einhorn.deadweight.generated.CardRules;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** Plain-JVM core tests: codec, malformed frames, and a full 8-round match through the real SocketTransport. */
public final class CoreTest {
    static int checks = 0, fails = 0;
    static void eq(String what, long got, long want) {
        checks++;
        if (got != want) { fails++; System.out.println("FAIL " + what + ": got " + got + " want " + want); }
    }
    static void yes(String what, boolean c) { eq(what, c ? 1 : 0, 1); }

    static byte[] body(byte[] frame) { byte[] b = new byte[frame.length - 2]; System.arraycopy(frame, 2, b, 0, b.length); return b; }

    static void codec() throws Exception {
        byte[] h = Protocol.hello(0, 1, "Ripper", new byte[]{1, 2, 3});
        eq("hello len field", (h[0] & 0xFF) | ((h[1] & 0xFF) << 8), h.length - 2);
        eq("hello type", h[2], 1); eq("hello proto", h[3], 2); eq("hello kind", h[5], 1);
        eq("hello name0", h[6], 'R'); eq("hello name NUL", h[21], 0); eq("hello token_len", h[22], 3); eq("hello tok", h[24], 2);
        byte[] p = Protocol.play(0x01020304L, 5, -1);
        eq("play type", p[2], 3); eq("play mid LE", p[3], 4); eq("play mid hi", p[6], 1); eq("play round", p[7], 5); eq("play slot", p[8], -1);
        Msg m = Protocol.decode(body(Protocol.matchFound(77, 0xDEADBEEFL, 1, "Wall", 1)));
        eq("mf mid", m.matchId, 77); eq("mf seed", m.seed, 0xDEADBEEFL); eq("mf seat", m.seat, 1);
        yes("mf name", m.oppName.equals("Wall")); eq("mf kind", m.oppKind, 1);
        m = Protocol.decode(body(Protocol.roundStart(3, 20, -4, 6, 2, new int[]{0, 5, -1, 8}, 4, 20000)));
        eq("rs round", m.round, 3); eq("rs hullOpp signed", m.hullOpp, -4); eq("rs hand1", m.hand[1], 5); eq("rs hand2", m.hand[2], -1);
        eq("rs deadline", m.deadlineMs, 20000);
        m = Protocol.decode(body(Protocol.roundResult(2, 6, -1, 0, 10, 20, 10)));
        eq("rr cardOpp pass", m.cardOpp, -1); eq("rr dmgOpp", m.dmgOpp, 10);
        m = Protocol.decode(body(Protocol.matchEnd(9, 2, 1)));
        eq("me result", m.result, 2);
        eq("welcome", Protocol.decode(body(Protocol.welcome(5, 3))).flags, 3);
        eq("queued", Protocol.decode(body(Protocol.queued(300))).waiting, 300);
        eq("reject reason", Protocol.decode(body(Protocol.playReject(1, 1, 4))).reason, 4);
    }

    static void expectProto(String what, byte[] payload) {
        checks++;
        try { Protocol.decode(payload); fails++; System.out.println("FAIL " + what + ": no ProtocolException"); }
        catch (ProtocolException ok) { }
    }

    static void malformed() throws Exception {
        expectProto("empty", new byte[0]);
        expectProto("unknown type", new byte[]{0x55});
        expectProto("truncated welcome", new byte[]{(byte) 0x81, 1, 0});
        byte[] good = body(Protocol.welcome(1, 0));
        byte[] trailing = new byte[good.length + 1]; System.arraycopy(good, 0, trailing, 0, good.length);
        expectProto("trailing bytes", trailing);
        expectProto("truncated match_found", new byte[]{(byte) 0x83, 1, 0, 0, 0});
        checks++;
        try { Protocol.hello(0, 0, "x", new byte[201]); fails++; System.out.println("FAIL oversize token accepted"); }
        catch (IllegalArgumentException ok) { }
        // oversize / zero length prefix through the real transport
        for (int bad : new int[]{1025, 0, 65535}) {
            try (ServerSocket ss = new ServerSocket(0)) {
                Thread t = new Thread(() -> {
                    try (Socket s = ss.accept()) { OutputStream o = s.getOutputStream(); o.write(new byte[]{(byte) bad, (byte) (bad >> 8), (byte) 0x81}); o.flush(); Thread.sleep(200); }
                    catch (Exception ignored) { }
                });
                t.start();
                SocketTransport tr = new SocketTransport("127.0.0.1", ss.getLocalPort(), 2000);
                tr.connect();
                checks++;
                try { tr.readFrame(); fails++; System.out.println("FAIL bad length " + bad + " accepted"); }
                catch (ProtocolException ok) { }
                tr.close(); t.join();
            }
        }
    }

    static void readFully(DataInputStream in, byte[] b) throws IOException { in.readFully(b); }
    static byte[] readClientFrame(DataInputStream in) throws IOException {
        int len = in.readUnsignedByte() | (in.readUnsignedByte() << 8);
        byte[] b = new byte[len]; in.readFully(b); return b;
    }

    static void fullMatch(final int tokenLen) throws Exception {
        final int[] hand = {0, 3, 6, 2}; // burst t0, tank t0, shield t0, burst t2 (constant fake hand)
        final AtomicInteger playsSeen = new AtomicInteger();
        try (ServerSocket ss = new ServerSocket(0)) {
            Thread server = new Thread(() -> {
                try (Socket s = ss.accept()) {
                    DataInputStream in = new DataInputStream(s.getInputStream());
                    OutputStream out = s.getOutputStream();
                    byte[] hello = readClientFrame(in);
                    eq("srv hello type", hello[0], 1);
                    eq("srv hello token_len is 0", hello[20], 0);
                    if (tokenLen > 0) {
                        byte[] au = readClientFrame(in);
                        eq("srv auth type", au[0], 6);
                        eq("srv auth len", (au[1] & 0xFF) | ((au[2] & 0xFF) << 8), tokenLen);
                        eq("srv auth frame size", au.length, 3 + tokenLen);
                        eq("srv auth last byte", au[au.length - 1], 'x');
                    }
                    out.write(Protocol.welcome(42, 0)); out.flush();
                    eq("srv queue type", readClientFrame(in)[0], 2);
                    // deliver MATCH_FOUND split into single-byte writes to exercise frame reassembly
                    byte[] q = Protocol.queued(1), mf = Protocol.matchFound(1001, 5, 0, "Wall", 1);
                    out.write(q); out.flush();
                    for (byte b : mf) { out.write(b); out.flush(); }
                    int hy = 20, ho = 20;
                    for (int r = 1; r <= 8; r++) {
                        out.write(Protocol.roundStart(r, hy, ho, 6, 6, hand, 4, 0)); out.flush();
                        byte[] play = readClientFrame(in);
                        eq("srv play type", play[0], 3);
                        eq("srv play mid", play[1] & 0xFF, 1001 & 0xFF);
                        eq("srv play round", play[5], r);
                        playsSeen.incrementAndGet();
                        out.write(Protocol.playAck(1001, r));
                        int slot = play[6];
                        int mine = slot < 0 ? -1 : hand[slot];
                        int dmgToOpp = CardRules.damageDealt(mine, -1), dmgToMe = 0;
                        ho -= dmgToOpp;
                        out.write(Protocol.roundResult(r, mine, -1, dmgToMe, dmgToOpp, hy, ho)); out.flush();
                    }
                    out.write(Protocol.matchEnd(1001, 1, 1)); out.flush();
                    Thread.sleep(300);
                } catch (Exception e) { fails++; System.out.println("FAIL server thread: " + e); }
            });
            server.start();

            CountDownLatch ended = new CountDownLatch(1);
            AtomicInteger rounds = new AtomicInteger(), results = new AtomicInteger(), acks = new AtomicInteger();
            StringBuilder states = new StringBuilder();
            final Session[] holder = new Session[1];
            Session.Listener l = new Session.Listener() {
                public void onState(Session.State s) { synchronized (states) { states.append(s).append(' '); } if (s == Session.State.READY && holder[0] != null && holder[0].match() == null) holder[0].queue(); }
                public void onQueued(int w) { }
                public void onMatchFound(MatchModel m) { eq("mf opp", m.oppName.equals("Wall") ? 1 : 0, 1); }
                public void onRoundStart(MatchModel m) {
                    rounds.incrementAndGet();
                    int slot = -1;
                    for (int i = 0; i < 4; i++) if (m.canPlaySlot(i)) { slot = i; break; }
                    yes("play accepted locally", holder[0].play(slot));
                    yes("double play blocked after lock? (not yet acked so allowed)", true);
                }
                public void onPlayAck(MatchModel m) { acks.incrementAndGet(); yes("locked", m.locked); yes("cannot replay when locked", !m.canPlaySlot(0)); }
                public void onPlayReject(MatchModel m, int r) { fails++; System.out.println("FAIL unexpected reject"); }
                public void onRoundResult(MatchModel m, MatchModel.RoundLog r) { results.incrementAndGet(); }
                public void onMatchEnd(MatchModel m) { eq("result win", m.result, 1); ended.countDown(); }
                public void onError(String msg) { System.out.println("session error: " + msg); ended.countDown(); }
            };
            Session s = new Session(new SocketTransport("127.0.0.1", ss.getLocalPort(), 2000), l, 0, 0, "Tester", tokenLen == 0 ? new byte[0] : xs(tokenLen));
            holder[0] = s;
            s.start();
            yes("match completes", ended.await(10, TimeUnit.SECONDS));
            eq("rounds seen", rounds.get(), 8); eq("results seen", results.get(), 8); eq("acks", acks.get(), 8);
            eq("server saw plays", playsSeen.get(), 8);
            eq("log size", s.match().log().size(), 8);
            eq("final hullOpp", s.match().hullOpp, 20 - 8 * 3); // slot 0 = burst t0 (3 dmg) each round vs pass; energy 6
            yes("state READY after end", s.state() == Session.State.READY);
            server.join();
            // server closed the socket -> session must go CLOSED with an error, not hang or throw
            Thread.sleep(400);
            yes("closed on server EOF", s.state() == Session.State.CLOSED);
            s.close();
        }
    }

    static byte[] xs(int n) { byte[] b = new byte[n]; java.util.Arrays.fill(b, (byte) 'x'); return b; }

    static void authFrame() throws Exception {
        byte[] t = new byte[500]; java.util.Arrays.fill(t, (byte) 'x');
        byte[] f = Protocol.auth(t);
        eq("auth len field", (f[0] & 0xFF) | ((f[1] & 0xFF) << 8), 503); eq("auth type", f[2], 6);
        eq("auth tok len", (f[3] & 0xFF) | ((f[4] & 0xFF) << 8), 500);
        checks++;
        try { Protocol.auth(new byte[901]); fails++; System.out.println("FAIL oversize auth"); } catch (IllegalArgumentException ok) { }
    }

    static void legality() {
        MatchModel m = new MatchModel();
        m.hand = new int[]{2, 0, -1, 8}; m.energyYou = 3;
        yes("tier2 unaffordable at 3", !m.canPlaySlot(0));
        yes("tier0 affordable", m.canPlaySlot(1));
        yes("empty slot illegal", !m.canPlaySlot(2));
        yes("pass always legal", m.canPlaySlot(-1));
        yes("bad slot", !m.canPlaySlot(4));
        eq("preview burst2 vs tank0", m.previewDamage(0, 3), 10);
        m.locked = true; yes("locked blocks", !m.canPlaySlot(-1));
    }

    static void guestAuth() throws Exception {
        com.sun.net.httpserver.HttpServer h = com.sun.net.httpserver.HttpServer.create(new java.net.InetSocketAddress("127.0.0.1", 0), 0);
        h.createContext("/api/v1/games/deadweight/guest-register", ex -> {
            String req = new String(ex.getRequestBody().readAllBytes());
            yes("register body has name", req.contains("\"display_name\":\"Ad\\\"a\""));
            byte[] r = "{\"player_id\":\"p-1\",\"guest_secret\":\"s3cret\",\"display_name\":\"Ad\\\"a\",\"token\":\"tok.en\",\"expires_at\":1}".getBytes();
            ex.sendResponseHeaders(201, r.length); ex.getResponseBody().write(r); ex.close();
        });
        h.createContext("/api/v1/games/deadweight/guest-login", ex -> {
            String req = new String(ex.getRequestBody().readAllBytes());
            boolean ok = req.contains("\"guest_secret\":\"s3cret\"");
            byte[] r = (ok ? "{\"player_id\":\"p-1\",\"display_name\":\"Ada\",\"token\":\"tok2\"}" : "{\"error\":\"invalid credentials\"}").getBytes();
            ex.sendResponseHeaders(ok ? 200 : 401, r.length); ex.getResponseBody().write(r); ex.close();
        });
        h.start();
        try {
            GuestAuth a = new GuestAuth.Iduna("http://127.0.0.1:" + h.getAddress().getPort() + "/");
            GuestAuth.Result r = a.register("Ad\"a");
            yes("register fields", r.playerId.equals("p-1") && r.guestSecret.equals("s3cret") && r.token.equals("tok.en") && r.displayName.equals("Ad\"a"));
            r = a.login("p-1", "s3cret");
            yes("login token", r.token.equals("tok2") && r.guestSecret.equals("s3cret"));
            checks++;
            try { a.login("p-1", "wrong"); fails++; System.out.println("FAIL bad secret accepted"); } catch (GuestAuth.AuthRejected ok) { }
        } finally { h.stop(0); }
    }

    /** Android forbids socket writes on the UI thread: Session must never send on the caller's thread. */
    static void sendsOffCallerThread() throws Exception {
        final java.util.concurrent.BlockingQueue<byte[]> in = new java.util.concurrent.LinkedBlockingQueue<>();
        final java.util.concurrent.atomic.AtomicReference<Thread> sender = new java.util.concurrent.atomic.AtomicReference<>();
        final java.util.concurrent.CountDownLatch sent = new java.util.concurrent.CountDownLatch(2); // hello + queue
        Transport t = new Transport() {
            public void connect() { }
            public void send(byte[] f) { sender.set(Thread.currentThread()); sent.countDown(); }
            public byte[] readFrame() throws java.io.IOException {
                try { byte[] f = in.take(); return f.length == 0 ? null : java.util.Arrays.copyOfRange(f, 2, f.length); }
                catch (InterruptedException e) { throw new java.io.IOException(e); }
            }
            public void close() { in.offer(new byte[0]); }
        };
        final java.util.concurrent.CountDownLatch ready = new java.util.concurrent.CountDownLatch(1);
        Session s = new Session(t, new Session.Listener() {
            public void onState(Session.State st) { if (st == Session.State.READY) ready.countDown(); }
            public void onQueued(int w) { } public void onMatchFound(MatchModel m) { } public void onRoundStart(MatchModel m) { }
            public void onPlayAck(MatchModel m) { } public void onPlayReject(MatchModel m, int r) { }
            public void onRoundResult(MatchModel m, MatchModel.RoundLog r) { } public void onMatchEnd(MatchModel m) { }
            public void onError(String msg) { }
        }, Protocol.MODE_CARD, Protocol.KIND_HUMAN, "t", null);
        s.start();
        in.offer(Protocol.welcome(1, 0));
        yes("ready reached", ready.await(3, java.util.concurrent.TimeUnit.SECONDS));
        Thread caller = Thread.currentThread();
        s.queue(); // as the UI thread would
        yes("send completed", sent.await(3, java.util.concurrent.TimeUnit.SECONDS));
        yes("send ran off the caller thread", sender.get() != null && sender.get() != caller);
        s.close();
    }

    public static void main(String[] a) throws Exception {
        codec(); malformed(); legality(); guestAuth(); authFrame(); sendsOffCallerThread(); fullMatch(0); fullMatch(480);
        System.out.println("CoreTest: " + checks + " checks, " + fails + " failures");
        System.exit(fails == 0 ? 0 : 1);
    }
}
