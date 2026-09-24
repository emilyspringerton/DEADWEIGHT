// account.ts -- real IDUNA online accounts for the DEADWEIGHT web client (S537). Founder
// real-time: "add iduna online accounts / add social features / profiles / friends / friendly
// challenges (duels) / for DEADWEIGHT / WOTAN". Mirrors apps/gui/main.c's own real, shipped
// account flow (auto guest-register, a persisted secret for silent re-login, "Claim Account" =
// guest-upgrade) -- this is the browser-client equivalent, not a new design. The resulting
// player_id/token is the exact same identity WOTAN's friends.html/profile.html operate on
// (IDUNA internal/http/handlers/game_online.go + game_social.go).
//
// Honest limit: IDUNA does not send CORS headers on /api/v1/games/, so a real cross-origin
// deployment (this static site on one origin, IDUNA on another) needs either a same-origin
// reverse proxy (WOTAN's own ops/nginx-wotan.conf /api/ pattern) or IDUNA-side CORS support --
// neither exists yet. Same-origin (idunaBase = '') or local dev against IDUNA directly both work.

const GAME = 'deadweight';
const STORAGE_KEY = 'dw_account_v1';

export interface Account {
    playerID: string;
    guestSecret: string; // empty once linked to email -- guest-login no longer applies
    displayName: string;
    token: string;
    expiresAt: number; // unix seconds
    emailLinked: boolean;
}

function loadStored(): Account | null {
    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        return raw ? (JSON.parse(raw) as Account) : null;
    } catch (e) {
        return null;
    }
}

function persist(a: Account) {
    try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(a));
    } catch (e) {
        /* private browsing / storage disabled -- account still works for this page load */
    }
}

export function clearAccount() {
    try {
        localStorage.removeItem(STORAGE_KEY);
    } catch (e) {
        /* nothing to clean up */
    }
}

async function api(idunaBase: string, path: string, opts?: RequestInit): Promise<any> {
    const res = await fetch(idunaBase + '/api/v1/games/' + GAME + path, opts);
    let body: any = null;
    try {
        body = await res.json();
    } catch (e) {
        /* no body, e.g. a plain error page from a misconfigured idunaBase */
    }
    if (!res.ok) {
        const msg = (body && (body.error || body.message)) || res.statusText || 'HTTP ' + res.status;
        throw new Error(msg);
    }
    return body;
}

async function guestRegister(idunaBase: string, name: string): Promise<Account> {
    const body = await api(idunaBase, '/guest-register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ display_name: name }),
    });
    return {
        playerID: body.player_id,
        guestSecret: body.guest_secret,
        displayName: body.display_name,
        token: body.token,
        expiresAt: body.expires_at,
        emailLinked: false,
    };
}

async function guestLogin(idunaBase: string, playerID: string, guestSecret: string): Promise<Account> {
    const body = await api(idunaBase, '/guest-login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ player_id: playerID, guest_secret: guestSecret }),
    });
    return {
        playerID,
        guestSecret,
        displayName: body.display_name,
        token: body.token,
        expiresAt: body.expires_at,
        emailLinked: false,
    };
}

// bootstrapAccount is the real "online accounts" entry point: a returning player's saved
// guest_secret refreshes their token (guest tokens expire after 24h -- IDUNA's guestTokenTTL);
// anyone new is guest-registered automatically, name-only, no email required (matches IDUNA's own
// S512 "zero-friction" convention). A player who already linked an email keeps that state.
export async function bootstrapAccount(idunaBase: string, name: string): Promise<Account> {
    const existing = loadStored();
    if (existing && existing.guestSecret) {
        try {
            const refreshed = await guestLogin(idunaBase, existing.playerID, existing.guestSecret);
            refreshed.emailLinked = existing.emailLinked;
            persist(refreshed);
            return refreshed;
        } catch (e) {
            // Stored secret no longer valid (account disabled, DB reset, etc.) -- fall through to
            // a fresh guest-register instead of getting the player permanently stuck.
        }
    }
    if (existing && existing.emailLinked && existing.token) {
        // An email-linked account has no guest_secret to refresh with; the caller should have
        // offered a real login form instead of silently minting a second, unrelated guest account.
        return existing;
    }
    const fresh = await guestRegister(idunaBase, name);
    persist(fresh);
    return fresh;
}

// loginWithEmail is the returning half for a player who already linked an email -- the same
// identity WOTAN's friends.html logs into.
export async function loginWithEmail(idunaBase: string, email: string, password: string): Promise<Account> {
    const body = await api(idunaBase, '/email-login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ email, password }),
    });
    const a: Account = {
        playerID: body.player_id,
        guestSecret: '',
        displayName: body.display_name,
        token: body.token,
        expiresAt: body.expires_at,
        emailLinked: true,
    };
    persist(a);
    return a;
}

// linkEmail upgrades the CURRENT guest account in place (same player_id, same match/ticket
// history) -- the browser-client equivalent of dw_gui's real, shipped "Claim Account" flow.
export async function linkEmail(idunaBase: string, current: Account, email: string, password: string): Promise<Account> {
    const body = await api(idunaBase, '/guest-upgrade', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + current.token },
        body: JSON.stringify({ email, password }),
    });
    const a: Account = {
        playerID: body.player_id,
        guestSecret: current.guestSecret,
        displayName: body.display_name,
        token: body.token,
        expiresAt: body.expires_at,
        emailLinked: true,
    };
    persist(a);
    return a;
}
