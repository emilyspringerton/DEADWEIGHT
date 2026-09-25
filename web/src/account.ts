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

// ApiError carries the real HTTP status alongside the message, so a caller can branch on it (e.g.
// claimOrLogin below telling a 409 email-conflict apart from any other failure) instead of
// fragile string-matching on the error text.
export class ApiError extends Error {
    status: number;
    constructor(status: number, message: string) {
        super(message);
        this.status = status;
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
        throw new ApiError(res.status, msg);
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

// claimOrLogin is linkEmail's real, honest completion (2026-09-25, founder real-time: "on the
// client if there is an account it should try to log you in with the email and password"). A 409
// from guest-upgrade means this exact email already belongs to SOME OTHER player_id -- most often
// a real, pre-existing account (this same player registered generically through IDUNA's SSO page
// on WOTAN, or is reinstalling/switching machines and already claimed this email before). Rather
// than dead-ending the player on "email already registered", this switches the active session to
// that pre-existing identity via a plain email-login with the same credentials they just typed --
// IDUNA's own claimGamePlayer (game_online.go) then claims it for DEADWEIGHT on the spot if it
// was never scoped to a game yet. The fresh guest account this session started as is simply
// abandoned (never deleted server-side, just no longer the active one) -- same "no server-side
// merge" safety boundary IDUNA's own claim-on-login fix deliberately drew, so two genuinely
// different players' match/ticket histories can never accidentally combine.
export async function claimOrLogin(idunaBase: string, current: Account, email: string, password: string): Promise<Account> {
    try {
        return await linkEmail(idunaBase, current, email, password);
    } catch (e) {
        if (e instanceof ApiError && e.status === 409) {
            return await loginWithEmail(idunaBase, email, password);
        }
        throw e;
    }
}

// isValidDisplayName mirrors core/account_rules.c's real, checked-in is_valid_display_name
// (generated from PARENA/stdlib/deadweight/account_rules.prn, C target only) exactly: 1-16
// characters, no control bytes -- same rule IDUNA's own server-side cleanDisplayName enforces.
//
// Hand-written here, NOT PARENA-generated, by necessity rather than choice: PARENA's TypeScript
// emitter v0 cannot emit this function (or anything depending on stdlib/string.prn) yet --
// string.prn's own length/char-at are declared with `@ Region` (needed for the C target's real
// region-safety verification), and the TS emitter hard-rejects ANY region-annotated parameter --
// confirmed by trying to build string.prn alone to .ts, which fails the exact same way, even
// though the emitter's own doc comment (src/emit_ts.c) says region annotations should be a real
// no-op for a garbage-collected target, not an error. Real, previously-undiscovered PARENA
// compiler gap, named honestly (EMILY/BACKLOG.md) rather than routed around by silently faking a
// "ported to TS" claim. When that gap is fixed, this function should be deleted and replaced with
// a real `import { isValidDisplayName } from './generated/AccountRules.js'`.
export function isValidDisplayName(s: string): boolean {
    if (s.length < 1 || s.length > 16) return false;
    for (let i = 0; i < s.length; i++) {
        const c = s.charCodeAt(i);
        if (c < 32 || c === 127) return false;
    }
    return true;
}

// registerAndClaim is the real "Create Account" flow (2026-09-25, founder real-time: "it should
// let me create a deadweight account right there with a button"): registers a fresh guest with
// the player's OWN chosen name (guestRegister already supported an explicit name, it was just
// only ever called with S512's auto-generated lore name at boot) and immediately claims it with
// email/password in the same action -- one button instead of the existing two-step
// guest-boot-then-link-later flow. claimOrLogin's own 409 fallback still applies here (a chosen
// name that happens to belong to an account with this same email already gets logged into that
// pre-existing identity rather than dead-ending).
export async function registerAndClaim(idunaBase: string, name: string, email: string, password: string): Promise<Account> {
    const fresh = await guestRegister(idunaBase, name);
    return await claimOrLogin(idunaBase, fresh, email, password);
}
