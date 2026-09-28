// sso.ts -- IDUNA SSO for the DEADWEIGHT browser client (docs/NATIVE_WASM_CLIENT_NORTHSTAR.md
// item 3). Same real, live pattern WOTAN/friends.html already proved end to end: redirect to
// IDUNA's hosted login (https://iam.okemily.com/?redirect_uri=<this page>), read the token back
// off the URL fragment (#sso_token=...&player_id=...&display_name=...), then exchange it for a
// real DEADWEIGHT player token via account.ts's loginWithSso (POST /api/v1/games/deadweight/
// sso-exchange). This module only owns the generic-IDUNA-session half (WOTAN's js/iduna-sso.js,
// ported to TS, same storage-key shape, this site's own origin so no collision) -- the exchange
// itself lives in account.ts next to every other Account-producing flow.
//
// Ported rather than shared as a literal file because this is a TS-module site (WOTAN's copy is a
// plain <script> global) -- kept behaviorally identical on purpose, not reinvented.

const TOKEN_KEY = 'dw_iduna_sso_token';
const PLAYER_ID_KEY = 'dw_iduna_sso_player_id';
const DISPLAY_NAME_KEY = 'dw_iduna_sso_display_name';

export interface IdunaSession {
    token: string;
    playerID: string;
    displayName: string;
}

function storageGet(key: string): string {
    try { return localStorage.getItem(key) || ''; } catch (e) { return ''; }
}
function storageSet(key: string, val: string) {
    try {
        if (val) localStorage.setItem(key, val);
        else localStorage.removeItem(key);
    } catch (e) {
        /* private browsing / storage disabled */
    }
}

/** The shared, sticky IDUNA session -- null if nothing is stored. */
export function getIdunaSession(): IdunaSession | null {
    const token = storageGet(TOKEN_KEY);
    const playerID = storageGet(PLAYER_ID_KEY);
    if (!token || !playerID) return null;
    return { token, playerID, displayName: storageGet(DISPLAY_NAME_KEY) };
}

export function setIdunaSession(token: string, playerID: string, displayName: string) {
    storageSet(TOKEN_KEY, token);
    storageSet(PLAYER_ID_KEY, playerID);
    storageSet(DISPLAY_NAME_KEY, displayName || '');
}

export function clearIdunaSession() {
    storageSet(TOKEN_KEY, '');
    storageSet(PLAYER_ID_KEY, '');
    storageSet(DISPLAY_NAME_KEY, '');
}

/** Builds the "Sign in with IDUNA" link target, redirecting back to this exact page. */
export function buildSsoURL(): string {
    const redirect = location.origin + location.pathname;
    const params = new URLSearchParams({ redirect_uri: redirect });
    return 'https://iam.okemily.com/?' + params.toString();
}

/** Reads a fresh SSO return off the URL fragment, stores it, and scrubs the fragment so a
 * refresh/share never carries the token in the URL. Returns null if no fragment is present --
 * callers should fall back to getIdunaSession() for a sticky session from an earlier visit. */
export function handleIdunaSsoReturn(): IdunaSession | null {
    const frag = new URLSearchParams(location.hash.slice(1));
    const token = frag.get('sso_token');
    const playerID = frag.get('player_id');
    if (!token || !playerID) return null;
    const displayName = frag.get('display_name') || '';
    setIdunaSession(token, playerID, displayName);
    history.replaceState(null, '', location.pathname + location.search);
    return { token, playerID, displayName };
}
