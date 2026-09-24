// DEADWEIGHT browser client — the game state machine. Talks to dw_server through a WebSocket,
// via bridge/ws-tcp-bridge.js (dw_server itself only ever speaks raw TCP; the bridge is a dumb,
// protocol-agnostic byte relay, not a second implementation of this protocol — see that file's
// own header comment). Card rules/catalog come from generated/CardRules.ts + generated/cards.json
// (both PARENA-compiled or PARENA-table-derived, never hand-duplicated here).

import * as proto from './proto.js';
import type { ServerFrame } from './proto.js';

export type ClientState = 'connecting' | 'ready' | 'queued' | 'in_match' | 'error' | 'closed';

export interface ClientEvents {
    onState?(s: ClientState): void;
    onQueued?(waiting: number): void;
    onMatchFound?(f: proto.MatchFound): void;
    onRoundStart?(f: proto.RoundStart): void;
    onPlayReject?(f: proto.PlayReject): void;
    onRoundResult?(f: proto.RoundResult): void;
    onMatchEnd?(f: proto.MatchEnd): void;
    onError?(f: proto.ErrorMsg): void;
    onLog?(line: string): void;
}

export class DeadweightClient {
    private ws: WebSocket | null = null;
    private decoder = new proto.FrameDecoder();
    private state: ClientState = 'connecting';
    matchId = 0;
    round = 0;
    seat = 0;

    constructor(private bridgeUrl: string, private ev: ClientEvents) {}

    connect(name: string, token: string = '') {
        this.setState('connecting');
        this.ws = new WebSocket(this.bridgeUrl);
        this.ws.binaryType = 'arraybuffer';
        this.ws.onopen = () => {
            this.ev.onLog?.(`connected to bridge ${this.bridgeUrl}`);
            // mode 0 = random queue, kind 0 = human. token is a real IDUNA player token (S537,
            // account.ts) when one was obtained; empty still works against a --no-auth server,
            // matching the GUI/Android clients' own documented "name-only play works with
            // --no-auth servers" precedent — apps/gui/main.c's own header comment.
            this.send(proto.encodeHello(0, 0, name, token));
        };
        this.ws.onmessage = (ev) => {
            const chunk = new Uint8Array(ev.data as ArrayBuffer);
            this.decoder.push(chunk, (typeAndPayload) => this.handleFrame(proto.decodeServerFrame(typeAndPayload)));
        };
        this.ws.onclose = () => {
            this.setState('closed');
            this.ev.onLog?.('connection closed');
        };
        this.ws.onerror = () => {
            this.setState('error');
            this.ev.onLog?.('websocket error');
        };
    }

    queue(sameDeck?: 0 | 1, matchToken?: string) {
        this.send(proto.encodeQueue(sameDeck, matchToken));
    }

    getState(): ClientState {
        return this.state;
    }

    play(slot: number) {
        this.send(proto.encodePlay(this.matchId, this.round, slot));
    }

    leave() {
        this.send(proto.encodeLeave());
    }

    private send(bytes: Uint8Array) {
        // .slice() copies into a plain ArrayBuffer-backed view -- newer lib.dom.d.ts typings
        // for WebSocket.send() no longer accept Uint8Array<ArrayBufferLike> directly (it could be
        // backed by a SharedArrayBuffer, which send() genuinely can't take).
        if (this.ws && this.ws.readyState === WebSocket.OPEN) this.ws.send(bytes.slice().buffer);
    }

    private setState(s: ClientState) {
        this.state = s;
        this.ev.onState?.(s);
    }

    private handleFrame(f: ServerFrame) {
        switch (f.type) {
            case 'WELCOME':
                this.setState('ready');
                this.ev.onLog?.(`WELCOME session=${f.sessionId} fastForward=${f.fastForward} authRequired=${f.authRequired}`);
                break;
            case 'QUEUED':
                this.setState('queued');
                this.ev.onQueued?.(f.waiting);
                break;
            case 'MATCH_FOUND':
                this.matchId = f.matchId;
                this.seat = f.seat;
                this.setState('in_match');
                this.ev.onMatchFound?.(f);
                break;
            case 'ROUND_START':
                this.round = f.round;
                this.ev.onRoundStart?.(f);
                break;
            case 'PLAY_ACK':
                this.ev.onLog?.(`play acked, round ${f.round}`);
                break;
            case 'PLAY_REJECT':
                this.ev.onPlayReject?.(f);
                break;
            case 'ROUND_RESULT':
                this.ev.onRoundResult?.(f);
                break;
            case 'MATCH_END':
                this.setState('ready');
                this.ev.onMatchEnd?.(f);
                break;
            case 'ERROR':
                this.setState('error');
                this.ev.onError?.(f);
                break;
            case 'PONG':
                break;
            default:
                this.ev.onLog?.(`unhandled/unimplemented frame (draft or unknown): ${JSON.stringify(f)}`);
        }
    }
}
