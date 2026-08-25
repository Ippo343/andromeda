#!/usr/bin/env node
'use strict';

// Node bridge between a browser and the env:native_runtime binary (built via
// `pio run -e native_runtime`): hosts the real WebSocket/HTTP server that
// binary has no socket stack of its own to provide (its comms.cpp
// equivalent - see platformio.ini's env:native_runtime comment and the plan
// at would-it-be-feasible-nifty-teapot.md), serves the *existing* data/
// static files unmodified, and forwards newline-delimited JSON both ways
// between connected browsers and the native core's stdio.
//
// Usage: node tools/native-bridge/server.js [--model="Andromeda Mk1"]
//   PORT env var overrides the default HTTP port (8080).

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');
const readline = require('readline');
const { WebSocketServer } = require('ws');

const REPO_ROOT = path.resolve(__dirname, '..', '..');
const DATA_DIR = path.join(REPO_ROOT, 'data');
const WEB_DIR = path.join(__dirname, 'web');
const NATIVE_BINARY = path.join(
    REPO_ROOT, '.pio', 'build', 'native_runtime',
    process.platform === 'win32' ? 'program.exe' : 'program'
);

const PORT = process.env.PORT ? parseInt(process.env.PORT, 10) : 8080;

const modelArg = process.argv.find((a) => a.startsWith('--model='));
const nativeArgs = modelArg ? [modelArg] : [];

// Mirrors src/comms.cpp's setupRoutes() STATIC_FILE_ROUTE list exactly -
// keep both in sync; a file added to data/ needs a route in both places or
// it 404s in one of the two environments (same gap test_static_assets.cpp
// guards between index.html and comms.cpp).
const STATIC_ROUTES = {
    '/': { file: 'index.html', type: 'text/html' },
    '/index.html': { file: 'index.html', type: 'text/html' },
    '/wifi-setup.html': { file: 'wifi-setup.html', type: 'text/html' },
    '/fonts/cinzel.woff2': { file: 'fonts/cinzel.woff2', type: 'font/woff2' },
    '/js/utils.js': { file: 'js/utils.js', type: 'application/javascript' },
    '/js/controls-logic.js': { file: 'js/controls-logic.js', type: 'application/javascript' },
    '/js/controls.js': { file: 'js/controls.js', type: 'application/javascript' },
    '/js/wifi.js': { file: 'js/wifi.js', type: 'application/javascript' },
    '/css/common.css': { file: 'css/common.css', type: 'text/css' },
    '/css/controls.css': { file: 'css/controls.css', type: 'text/css' },
    '/css/wifi.css': { file: 'css/wifi.css', type: 'text/css' },
};

// The visualizer page lives under tools/native-bridge/web/, not data/ - that
// directory is bundled verbatim into the LittleFS image uploaded to real
// hardware, so dev-only visualizer code has no business shipping there.
const VISUALIZER_ROUTES = {
    '/visualizer.html': { file: 'visualizer.html', type: 'text/html' },
    '/visualizer.js': { file: 'visualizer.js', type: 'application/javascript' },
};

if (!fs.existsSync(NATIVE_BINARY)) {
    console.error(`Native runtime binary not found at ${NATIVE_BINARY}`);
    console.error('Build it first: pio run -e native_runtime');
    process.exit(1);
}

console.log(`Spawning native runtime: ${NATIVE_BINARY} ${nativeArgs.join(' ')}`);
const child = spawn(NATIVE_BINARY, nativeArgs);

child.on('error', (err) => {
    console.error('Failed to spawn native runtime binary:', err);
    process.exit(1);
});

child.on('exit', (code, signal) => {
    console.error(`Native runtime exited (code=${code}, signal=${signal}) - the bridge can't recover from this, exiting too.`);
    process.exit(1);
});

child.stderr.pipe(process.stderr);

// --- stdout (native core) -> broadcast to all connected browsers ---------

// Cached so a browser that connects after the last state-changing event (or
// after the one-time geometry message) isn't left waiting indefinitely -
// mirrors comms.cpp's WS_EVT_CONNECT handler, which pushes current state
// immediately to a newly-connected client.
let lastStateLine = null;
let lastGeometryLine = null;
let lastFps = null;
let lastBrightness = null;

const clients = new Set();

function broadcast(line) {
    for (const client of clients) {
        if (client.readyState === client.OPEN) client.send(line);
    }
}

const rl = readline.createInterface({ input: child.stdout });
rl.on('line', (line) => {
    let msg;
    try {
        msg = JSON.parse(line);
    } catch (e) {
        console.error('Ignoring non-JSON line from native runtime:', line);
        return;
    }

    if (msg.type === 'state') {
        lastStateLine = line;
        lastFps = msg.fps;
        lastBrightness = msg.brightness;
    } else if (msg.type === 'geometry') {
        lastGeometryLine = line;
    }
    // 'frame' (and any future message type) is forwarded as-is, uncached -
    // there's no "current frame" concept worth replaying to a late joiner.

    broadcast(line);
});

// --- HTTP: static files + /fps + /brightness ------------------------------

function serveFile(res, filePath, contentType) {
    fs.readFile(filePath, (err, content) => {
        if (err) {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end('Not Found');
            return;
        }
        res.writeHead(200, { 'Content-Type': contentType });
        res.end(content);
    });
}

const server = http.createServer((req, res) => {
    const url = (req.url || '/').split('?')[0];

    if (url === '/fps') {
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end(lastFps === null ? 'NaN' : String(lastFps));
        return;
    }
    if (url === '/brightness') {
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end(lastBrightness === null ? '' : String(lastBrightness));
        return;
    }

    const staticRoute = STATIC_ROUTES[url];
    if (staticRoute) {
        serveFile(res, path.join(DATA_DIR, staticRoute.file), staticRoute.type);
        return;
    }

    const visualizerRoute = VISUALIZER_ROUTES[url];
    if (visualizerRoute) {
        serveFile(res, path.join(WEB_DIR, visualizerRoute.file), visualizerRoute.type);
        return;
    }

    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not Found');
});

// --- WebSocket: /ws --------------------------------------------------------

const wss = new WebSocketServer({ noServer: true });

server.on('upgrade', (req, socket, head) => {
    if (req.url !== '/ws') {
        socket.destroy();
        return;
    }
    wss.handleUpgrade(req, socket, head, (ws) => {
        clients.add(ws);

        if (lastStateLine) ws.send(lastStateLine);
        if (lastGeometryLine) ws.send(lastGeometryLine);

        ws.on('message', (data) => {
            // Forwarded verbatim to the native core's stdin - same wire
            // format WsCommandParser::parse()/parseBrightness() already
            // expect (see native-runtime.cpp's processIncomingLine()).
            child.stdin.write(data.toString() + '\n');
        });

        ws.on('close', () => clients.delete(ws));
    });
});

server.listen(PORT, () => {
    console.log(`Native bridge listening on http://localhost:${PORT}`);
});

process.on('SIGINT', () => {
    child.kill();
    process.exit(0);
});
