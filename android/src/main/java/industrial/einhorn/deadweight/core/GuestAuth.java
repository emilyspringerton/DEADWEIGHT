package industrial.einhorn.deadweight.core;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/** Guest account flow against IDUNA (docs/IDUNA_CONTRACT.md). Blocking: call from a background thread. */
public interface GuestAuth {
    /** tickets/isGuest mirror apps/gui/main.c's dwi_guest_register/login out_tickets/out_is_guest
     *  (JSON "tickets" int, "account_state" == "guest"). isGuest is what the Claim Account
     *  affordance gates on -- never "no email typed in", a returning claimed player must never see it. */
    final class Result {
        public final String playerId, guestSecret, displayName, token;
        public final int tickets;
        public final boolean isGuest;
        public Result(String playerId, String guestSecret, String displayName, String token, int tickets, boolean isGuest) {
            this.playerId = playerId; this.guestSecret = guestSecret; this.displayName = displayName; this.token = token;
            this.tickets = tickets; this.isGuest = isGuest;
        }
    }

    /** Mirrors dwi_redeem's out_tickets_granted/out_founder/out_balance. founder is real only if
     *  THIS redeem call returned founder:true -- not persisted/re-fetched on a later boot, same
     *  real limitation apps/gui/main.c's own A.is_founder has (session-local, not profile state). */
    final class RedeemResult {
        public final int ticketsGranted, balance;
        public final boolean founder;
        public RedeemResult(int ticketsGranted, boolean founder, int balance) {
            this.ticketsGranted = ticketsGranted; this.founder = founder; this.balance = balance;
        }
    }

    /** First launch: create a guest. Persist playerId + guestSecret; losing them loses the account (by design). */
    Result register(String displayName) throws IOException;

    /** Later launches: resume the same player. Throws {@link AuthRejected} on 401 (wrong/lost secret). */
    Result login(String playerId, String guestSecret) throws IOException;

    /** Redeem a code for tickets (+ possibly founder status). Throws on -- an invalid/already-used/
     *  wrong-game code, same rc==-2 case apps/gui/main.c's do_redeem surfaces as a plain message. */
    RedeemResult redeem(String playerToken, String code) throws IOException;

    /** Claim Account: link email+password to the current guest player_id, returns a new token for
     *  the SAME player (tickets/stats/founder-flag/draft-run all carry over -- apps/gui/main.c's
     *  own dwi_guest_upgrade doc comment). Throws {@link EmailTaken} on 409 (this email already
     *  belongs to a DIFFERENT player_id) -- the caller should fall back to {@link #emailLogin},
     *  same real "login instead" UX apps/gui/main.c's do_link_email already establishes. */
    String upgrade(String playerToken, String email, String password) throws IOException;

    /** Sign in to an already-claimed (non-guest) account by email+password -- the do_link_email
     *  login-fallback path, and also usable as a standalone "I already have an account" entry. */
    String emailLogin(String email, String password) throws IOException;

    final class AuthRejected extends IOException {
        public AuthRejected(String m) { super(m); }
    }

    /** 409 from guest-upgrade: this email already belongs to a different player_id. Not a hard
     *  failure -- the real, intended response is to try {@link #emailLogin} with the same
     *  credentials (apps/gui/main.c's do_link_email "logged_in_instead" path). */
    final class EmailTaken extends IOException {
        public EmailTaken(String m) { super(m); }
    }

    final class Iduna implements GuestAuth {
        private final String base;

        public Iduna(String baseUrl) { this.base = baseUrl.endsWith("/") ? baseUrl.substring(0, baseUrl.length() - 1) : baseUrl; }

        @Override public Result register(String displayName) throws IOException {
            String r = post("/api/v1/games/deadweight/guest-register", null, "{\"display_name\":" + quote(displayName) + "}");
            return new Result(need(r, "player_id"), need(r, "guest_secret"), need(r, "display_name"), need(r, "token"),
                optInt(r, "tickets"), isGuestState(r));
        }

        @Override public Result login(String playerId, String guestSecret) throws IOException {
            String r = post("/api/v1/games/deadweight/guest-login", null,
                "{\"player_id\":" + quote(playerId) + ",\"guest_secret\":" + quote(guestSecret) + "}");
            return new Result(need(r, "player_id"), guestSecret, need(r, "display_name"), need(r, "token"),
                optInt(r, "tickets"), isGuestState(r));
        }

        @Override public RedeemResult redeem(String playerToken, String code) throws IOException {
            String r = post("/api/v1/games/deadweight/redeem", playerToken, "{\"code\":" + quote(code) + "}");
            return new RedeemResult(optInt(r, "tickets_granted"), optInt(r, "founder") != 0, optInt(r, "tickets"));
        }

        @Override public String upgrade(String playerToken, String email, String password) throws IOException {
            Resp r = postStatus("/api/v1/games/deadweight/guest-upgrade", playerToken,
                "{\"email\":" + quote(email) + ",\"password\":" + quote(password) + "}");
            if (r.code == 409) throw new EmailTaken("email already linked to another account");
            if (r.code != 200) throw new IOException("IDUNA " + r.code + ": " + r.body);
            return need(r.body, "token");
        }

        @Override public String emailLogin(String email, String password) throws IOException {
            String r = post("/api/v1/games/deadweight/email-login", null,
                "{\"email\":" + quote(email) + ",\"password\":" + quote(password) + "}");
            return need(r, "token");
        }

        /** Mirrors apps/gui/main.c's accountStateIsGuest(): "account_state" == "guest" (vs "base"). */
        private static boolean isGuestState(String json) {
            return "guest".equals(opt(json, "account_state"));
        }

        private String post(String path, String bearer, String json) throws IOException {
            Resp r = postStatus(path, bearer, json);
            if (r.code == 401) throw new AuthRejected("invalid credentials");
            if (r.code == 429) throw new IOException("rate limited, try again shortly");
            if (r.code < 200 || r.code > 299) throw new IOException("IDUNA " + r.code + ": " + r.body);
            return r.body;
        }

        private static final class Resp { final int code; final String body; Resp(int c, String b) { code = c; body = b; } }

        private Resp postStatus(String path, String bearer, String json) throws IOException {
            HttpURLConnection c = (HttpURLConnection) new URL(base + path).openConnection();
            try {
                c.setConnectTimeout(5000); c.setReadTimeout(8000);
                c.setRequestMethod("POST"); c.setDoOutput(true);
                c.setRequestProperty("Content-Type", "application/json");
                if (bearer != null && !bearer.isEmpty()) c.setRequestProperty("Authorization", "Bearer " + bearer);
                byte[] body = json.getBytes(StandardCharsets.UTF_8);
                try (OutputStream o = c.getOutputStream()) { o.write(body); }
                int code = c.getResponseCode();
                String resp = read(code >= 400 ? c.getErrorStream() : c.getInputStream());
                return new Resp(code, resp);
            } finally { c.disconnect(); }
        }

        private static String read(InputStream in) throws IOException {
            if (in == null) return "";
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[1024]; int n;
            while ((n = in.read(buf)) > 0 && b.size() < 65536) b.write(buf, 0, n);
            return new String(b.toByteArray(), StandardCharsets.UTF_8);
        }

        static String quote(String s) {
            StringBuilder sb = new StringBuilder("\"");
            for (char ch : s.toCharArray()) {
                if (ch == '"' || ch == '\\') sb.append('\\').append(ch);
                else if (ch < 0x20) sb.append(String.format("\\u%04x", (int) ch));
                else sb.append(ch);
            }
            return sb.append('"').toString();
        }

        /** Minimal extractor for a top-level string field in the flat JSON IDUNA returns. */
        static String need(String json, String key) throws IOException {
            String k = "\"" + key + "\"";
            int i = json.indexOf(k);
            if (i < 0) throw new IOException("IDUNA response missing " + key);
            i = json.indexOf(':', i + k.length());
            int q = i < 0 ? -1 : json.indexOf('"', i);
            if (q < 0) throw new IOException("IDUNA response bad " + key);
            StringBuilder sb = new StringBuilder();
            for (int j = q + 1; j < json.length(); j++) {
                char ch = json.charAt(j);
                if (ch == '\\' && j + 1 < json.length()) {
                    char nx = json.charAt(++j);
                    if (nx == 'u' && j + 4 < json.length()) { sb.append((char) Integer.parseInt(json.substring(j + 1, j + 5), 16)); j += 4; }
                    else sb.append(nx == 'n' ? '\n' : nx);
                } else if (ch == '"') return sb.toString();
                else sb.append(ch);
            }
            throw new IOException("IDUNA response unterminated " + key);
        }

        /** Same string field, but missing-key returns null instead of throwing (account_state is
         *  only ever present on register/login responses, never required to exist). */
        static String opt(String json, String key) {
            try { return need(json, key); } catch (IOException e) { return null; }
        }

        /** A top-level int/bool field ("tickets", "founder": true/false or 0/1) -- IDUNA never
         *  quotes these. Mirrors dw_json_int's own lenient behavior exactly: missing or unparsable
         *  silently defaults to 0 rather than failing the whole call -- a field this cosmetic (a
         *  ticket count, a founder flag) should never break a real login/register/redeem. */
        static int optInt(String json, String key) {
            String k = "\"" + key + "\"";
            int i = json.indexOf(k);
            if (i < 0) return 0;
            i = json.indexOf(':', i + k.length());
            if (i < 0) return 0;
            int j = i + 1;
            while (j < json.length() && Character.isWhitespace(json.charAt(j))) j++;
            if (json.regionMatches(j, "true", 0, 4)) return 1;
            if (json.regionMatches(j, "false", 0, 5)) return 0;
            int start = j;
            if (j < json.length() && json.charAt(j) == '-') j++;
            while (j < json.length() && Character.isDigit(json.charAt(j))) j++;
            return j == start ? 0 : Integer.parseInt(json.substring(start, j));
        }
    }
}
