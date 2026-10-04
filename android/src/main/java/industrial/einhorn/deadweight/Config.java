package industrial.einhorn.deadweight;

/** Single place for build-time defaults, matching apps/gui/main.c's own real production defaults
 *  (main(): host="okemily.com", port="6980", iduna_url="https://okemily.com") -- these are the
 *  real, live endpoints (dw-server.service, ops/systemd), not placeholders. Never shown to the
 *  player (S512 zero-friction auth is now real on Android too, see MainActivity's bootstrapAuth). */
final class Config {
    private Config() {}
    /** The DEADWEIGHT server on the EINHORN box (okemily.com -> 198.58.107.85). Emulator dev: use 10.0.2.2. */
    static final String DEFAULT_HOST = "dw.okemily.com";
    /** dw_server TCP port (ops/systemd/dw.env). */
    static final int DEFAULT_PORT = 7180;
    /** Empty = name-only (server must run --no-auth); "https://okemily.com" is the real, live
     *  production IDUNA -- was empty here (a real, found-live gap: Android had no IDUNA account at
     *  all by default, no tickets/redeem/claim, while the desktop client fixed this at S508d). */
    static final String DEFAULT_IDUNA_URL = "https://okemily.com";
}
