#!/usr/bin/env node
// ws-tcp-bridge.js — a dumb, protocol-agnostic WebSocket<->TCP relay for dw_server.
//
// Why this exists: browsers cannot open raw TCP sockets, only WebSocket/HTTP/fetch. dw_server
// (apps/server/main.c) is a plain TCP server (docs/WIRE_PROTOCOL.md) and stays that way — this
// bridge is deliberately NOT a second implementation of the wire protocol. It knows nothing about
// HELLO/QUEUE/PLAY/frame types; it just copies bytes in both directions, 1:1, between one
// WebSocket connection and one TCP connection to dw_server. The real protocol is implemented
// exactly once, in web/src/proto.ts (browser side) and core/ (server side).
//
// Usage: node ws-tcp-bridge.js [--ws-port 8765] [--tcp-host 127.0.0.1] [--tcp-port 7900]
'use strict';
const net = require('net');
const { WebSocketServer } = require('ws');

function arg(name, def) {
    const i = process.argv.indexOf(name);
    return i === -1 ? def : process.argv[i + 1];
}

const wsPort = parseInt(arg('--ws-port', '8765'), 10);
const tcpHost = arg('--tcp-host', '127.0.0.1');
const tcpPort = parseInt(arg('--tcp-port', '7900'), 10);

const wss = new WebSocketServer({ port: wsPort });
console.log(`ws-tcp-bridge: listening ws://0.0.0.0:${wsPort} -> tcp ${tcpHost}:${tcpPort}`);

wss.on('connection', (ws) => {
    const tcp = net.connect(tcpPort, tcpHost);
    let tcpReady = false;
    const pending = [];

    tcp.on('connect', () => {
        tcpReady = true;
        for (const buf of pending.splice(0)) tcp.write(buf);
    });

    tcp.on('data', (chunk) => {
        if (ws.readyState === ws.OPEN) ws.send(chunk);
    });

    tcp.on('close', () => ws.close());
    tcp.on('error', (err) => {
        console.error('tcp error:', err.message);
        ws.close();
    });

    ws.on('message', (data) => {
        const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
        if (tcpReady) tcp.write(buf);
        else pending.push(buf);
    });

    ws.on('close', () => tcp.destroy());
    ws.on('error', () => tcp.destroy());
});
