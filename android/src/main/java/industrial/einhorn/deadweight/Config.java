package industrial.einhorn.deadweight;

/** Single place for build-time defaults. PLACEHOLDERS: the real server/IDUNA endpoints are not deployed yet. */
final class Config {
    private Config() {}
    /** PLACEHOLDER: 10.0.2.2 is the Android emulator's alias for the dev machine's localhost. User-editable in-app. */
    static final String DEFAULT_HOST = "10.0.2.2";
    /** PLACEHOLDER: dw_server's real port is decided by lane B / deploy; user-editable in-app. */
    static final int DEFAULT_PORT = 6980;
    /** Empty = name-only (server must run --no-auth). e.g. "http://10.0.2.2:8080" for a dev IDUNA. */
    static final String DEFAULT_IDUNA_URL = "";
}
