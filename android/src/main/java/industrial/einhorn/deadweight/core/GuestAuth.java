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
    final class Result {
        public final String playerId, guestSecret, displayName, token;
        public Result(String playerId, String guestSecret, String displayName, String token) {
            this.playerId = playerId; this.guestSecret = guestSecret; this.displayName = displayName; this.token = token;
        }
    }

    /** First launch: create a guest. Persist playerId + guestSecret; losing them loses the account (by design). */
    Result register(String displayName) throws IOException;

    /** Later launches: resume the same player. Throws {@link AuthRejected} on 401 (wrong/lost secret). */
    Result login(String playerId, String guestSecret) throws IOException;

    final class AuthRejected extends IOException {
        public AuthRejected(String m) { super(m); }
    }

    final class Iduna implements GuestAuth {
        private final String base;

        public Iduna(String baseUrl) { this.base = baseUrl.endsWith("/") ? baseUrl.substring(0, baseUrl.length() - 1) : baseUrl; }

        @Override public Result register(String displayName) throws IOException {
            String r = post("/api/v1/games/deadweight/guest-register", "{\"display_name\":" + quote(displayName) + "}");
            return new Result(need(r, "player_id"), need(r, "guest_secret"), need(r, "display_name"), need(r, "token"));
        }

        @Override public Result login(String playerId, String guestSecret) throws IOException {
            String r = post("/api/v1/games/deadweight/guest-login",
                "{\"player_id\":" + quote(playerId) + ",\"guest_secret\":" + quote(guestSecret) + "}");
            return new Result(need(r, "player_id"), guestSecret, need(r, "display_name"), need(r, "token"));
        }

        private String post(String path, String json) throws IOException {
            HttpURLConnection c = (HttpURLConnection) new URL(base + path).openConnection();
            try {
                c.setConnectTimeout(5000); c.setReadTimeout(8000);
                c.setRequestMethod("POST"); c.setDoOutput(true);
                c.setRequestProperty("Content-Type", "application/json");
                byte[] body = json.getBytes(StandardCharsets.UTF_8);
                try (OutputStream o = c.getOutputStream()) { o.write(body); }
                int code = c.getResponseCode();
                String resp = read(code >= 400 ? c.getErrorStream() : c.getInputStream());
                if (code == 401) throw new AuthRejected("invalid credentials");
                if (code == 429) throw new IOException("rate limited, try again shortly");
                if (code < 200 || code > 299) throw new IOException("IDUNA " + code + ": " + resp);
                return resp;
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
    }
}
