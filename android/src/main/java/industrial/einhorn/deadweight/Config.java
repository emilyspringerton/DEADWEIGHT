package industrial.einhorn.deadweight;

/** Single place for build-time defaults. PLACEHOLDERS: the real server/IDUNA endpoints are not deployed yet. */
final class Config {
    private Config() {}
    /** The DEADWEIGHT server on the EINHORN box (okemily.com -> 198.58.107.85). Emulator dev: use 10.0.2.2. Editable in-app. */
    static final String DEFAULT_HOST = "okemily.com";
    /** dw_server TCP port (ops/systemd/dw.env). Editable in-app. */
    static final int DEFAULT_PORT = 6980;
    /** Empty = name-only (server must run --no-auth). e.g. "http://10.0.2.2:8080" for a dev IDUNA. */
    static final String DEFAULT_IDUNA_URL = "";
}
