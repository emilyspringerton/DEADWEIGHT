// social.ts -- friends, public profiles, and friendly-challenge duels for the DEADWEIGHT browser
// client (S537 continued). Talks to the exact same IDUNA routes WOTAN's friends.html/profile.html
// use (IDUNA internal/http/handlers/game_social.go, under /api/v1/games/deadweight/...) -- same
// identity, same server, a second real consumer rather than a parallel design.

const GAME = 'deadweight';

export interface Profile {
    player_id: string;
    display_name: string;
    kind: string;
    rating: number;
    wins: number;
    losses: number;
    draws: number;
    matches: number;
    friend_count: number;
}

export interface FriendRequest {
    id: number;
    requester_id: string;
    recipient_id: string;
    status: string;
    created_at: string;
}

export interface FriendSummary {
    player_id: string;
    display_name: string;
    rating: number;
}

export interface Duel {
    id: number;
    challenger_id: string;
    challenged_id: string;
    status: string;
    created_at: string;
    // match_token/match_token_expires_at (S537 Duel Phase 2): only present while status ===
    // "accepted" and the 15-min TTL hasn't lapsed (IDUNA's duelsList) -- absent/undefined
    // otherwise, whether pending, declined, or expired.
    match_token?: string;
    match_token_expires_at?: string;
}

async function api(idunaBase: string, token: string, path: string, opts?: RequestInit): Promise<any> {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    if (token) headers['Authorization'] = 'Bearer ' + token;
    const res = await fetch(idunaBase + '/api/v1/games/' + GAME + path, { ...opts, headers: { ...headers, ...(opts?.headers as any) } });
    let body: any = null;
    try {
        body = await res.json();
    } catch (e) {
        /* no body */
    }
    if (!res.ok) {
        const msg = (body && (body.error || body.message)) || res.statusText || 'HTTP ' + res.status;
        throw new Error(msg);
    }
    return body;
}

export function getProfile(idunaBase: string, playerID: string): Promise<Profile> {
    return api(idunaBase, '', '/players/' + encodeURIComponent(playerID) + '/profile');
}

export function listFriends(idunaBase: string, token: string): Promise<FriendSummary[]> {
    return api(idunaBase, token, '/friends');
}

export function removeFriend(idunaBase: string, token: string, playerID: string): Promise<any> {
    return api(idunaBase, token, '/friends/' + encodeURIComponent(playerID), { method: 'DELETE' });
}

export function listFriendRequests(idunaBase: string, token: string): Promise<{ incoming: FriendRequest[]; outgoing: FriendRequest[] }> {
    return api(idunaBase, token, '/friend-requests');
}

export function sendFriendRequest(idunaBase: string, token: string, toPlayerID: string): Promise<any> {
    return api(idunaBase, token, '/friend-requests', { method: 'POST', body: JSON.stringify({ to_player_id: toPlayerID }) });
}

export function respondFriendRequest(idunaBase: string, token: string, id: number, accept: boolean): Promise<any> {
    return api(idunaBase, token, '/friend-requests/' + id + '/' + (accept ? 'accept' : 'decline'), { method: 'POST' });
}

export function listDuels(idunaBase: string, token: string): Promise<Duel[]> {
    return api(idunaBase, token, '/duels');
}

export function createDuel(idunaBase: string, token: string, toPlayerID: string): Promise<any> {
    return api(idunaBase, token, '/duels', { method: 'POST', body: JSON.stringify({ to_player_id: toPlayerID }) });
}

export function respondDuel(idunaBase: string, token: string, id: number, accept: boolean): Promise<any> {
    return api(idunaBase, token, '/duels/' + id + '/' + (accept ? 'accept' : 'decline'), { method: 'POST' });
}
