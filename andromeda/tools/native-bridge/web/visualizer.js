// Browser LED visualizer for the native runtime bridge (tools/native-bridge/
// server.js). Deliberately opens its own independent /ws connection instead
// of hooking into data/js/controls.js's single ws.onmessage handler - the
// simplest way to add frame rendering without touching controls.js at all
// (see the plan at would-it-be-feasible-nifty-teapot.md, Part 5).

const SVG_NS = 'http://www.w3.org/2000/svg';
const canvas = document.getElementById('canvas');
const statusEl = document.getElementById('status');

// Flat array of <circle> elements, built once per 'geometry' message, in the
// same strip-major order native-runtime.cpp emits both 'geometry' and
// 'frame' messages in (see emitGeometryOnce()/frameCaptureHook()) - so
// circles[i] always corresponds to frame.leds[i].
let circles = [];

function buildCircles(geometry) {
    canvas.innerHTML = '';
    circles = [];

    const halfW = geometry.width_mm / 2;
    const halfH = geometry.height_mm / 2;
    canvas.setAttribute('viewBox', `${-halfW} ${-halfH} ${geometry.width_mm} ${geometry.height_mm}`);
    canvas.setAttribute('width', '600');
    canvas.setAttribute('height', Math.round(600 * (geometry.height_mm / geometry.width_mm)));

    // Sized relative to the device's physical footprint so dots stay
    // visible/proportional whether the model is a small test rig or a full
    // Andromeda sphere, clamped to a sane visual range either way.
    const radius = Math.min(12, Math.max(3, Math.max(geometry.width_mm, geometry.height_mm) * 0.015));

    for (const strip of geometry.strips) {
        for (const [x, y] of strip.points) {
            const circle = document.createElementNS(SVG_NS, 'circle');
            circle.setAttribute('cx', x);
            circle.setAttribute('cy', y);
            circle.setAttribute('r', radius);
            circle.setAttribute('fill', '#000');
            canvas.appendChild(circle);
            circles.push(circle);
        }
    }

    statusEl.textContent = `${circles.length} LEDs`;
}

function applyFrame(frame) {
    const leds = frame.leds;
    const n = Math.min(leds.length, circles.length);
    for (let i = 0; i < n; i++) {
        const [r, g, b] = leds[i];
        circles[i].setAttribute('fill', `rgb(${r},${g},${b})`);
    }
}

function connect() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

    ws.onopen = () => { statusEl.textContent = 'connected'; };
    ws.onclose = () => {
        statusEl.textContent = 'disconnected, retrying...';
        setTimeout(connect, 2000);
    };
    ws.onmessage = (event) => {
        let msg;
        try {
            msg = JSON.parse(event.data);
        } catch (e) {
            return;
        }
        if (msg.type === 'geometry') buildCircles(msg);
        else if (msg.type === 'frame') applyFrame(msg);
        // 'state' is ignored here - the existing controls page (index.html)
        // is what renders that.
    };
}

connect();
