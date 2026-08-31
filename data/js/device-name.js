// Device-name page wiring. Deliberately on its own page, off the main
// controls page: it holds the only free-text <input> in the UI, and iOS's
// captive-portal webview autofocuses the first text field of whatever page it
// loads (always "/") and zooms in. Keeping it here means "/" has no text
// input to grab. Uses the existing /ws WebSocket + state broadcast - the same
// {type:'device_name'} / {type:'reboot'} commands controls.js used to send.

let ws = null;
let editing = false;
let reconnectAttempt = 0;
let reconnectTimer = null;

function scheduleReconnect() {
    if (reconnectTimer) return;
    const delay = nextReconnectDelayMs(reconnectAttempt);
    reconnectAttempt++;
    reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connectWebSocket();
    }, delay);
}

function connectWebSocket() {
    document.body.classList.add('ws-disconnected');

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    let socket;
    try {
        socket = new WebSocket(`${protocol}//${window.location.host}/ws`);
    } catch (e) {
        // See controls.js's connectWebSocket() for why this guard exists - a synchronous
        // throw here previously killed the retry chain permanently.
        scheduleReconnect();
        return;
    }
    ws = socket;

    // See controls.js's connectWebSocket() for why this exists - a handshake stuck in
    // CONNECTING forever (no open/error/close) would otherwise never trigger a reconnect.
    const connectTimeout = setTimeout(() => {
        if (socket.readyState === WebSocket.CONNECTING) {
            try { socket.close(); } catch (e) { /* already closing/closed */ }
        }
    }, 10000);

    ws.onopen = () => {
        clearTimeout(connectTimeout);
        reconnectAttempt = 0;
        document.body.classList.remove('ws-disconnected');
    };
    ws.onerror = () => scheduleReconnect();
    ws.onclose = () => {
        clearTimeout(connectTimeout);
        document.body.classList.add('ws-disconnected');
        scheduleReconnect();
    };
    ws.onmessage = (event) => handleStateMessage(event.data);
}

function setRebootIndicator(show) {
    const row = document.getElementById('deviceNameRow');
    const btn = document.getElementById('deviceNameRebootBtn');
    if (!row || !btn) return;
    row.classList.toggle('has-reboot', show);
    btn.classList.toggle('visible', show);
}

function handleStateMessage(raw) {
    let msg;
    try {
        msg = JSON.parse(raw);
    } catch (e) {
        return;
    }
    if (msg.type !== 'state' || !msg.device) return;

    // A missing/non-string name here previously set the input's value to the literal text
    // "undefined" (HTMLInputElement.value stringifies whatever it's assigned) - and the blur
    // handler below then committed that string as the new device name.
    const input = document.getElementById('deviceNameInput');
    if (input && !editing && typeof msg.device.name === 'string') input.value = msg.device.name;

    const hint = document.getElementById('deviceUidHint');
    if (hint && typeof msg.device.uid === 'string') hint.textContent = `Device ID: ${msg.device.uid}`;

    setRebootIndicator(msg.device.nameRebootRequired);
}

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('logo').style.setProperty('--grad', randomGradient());

    const input = document.getElementById('deviceNameInput');

    input.addEventListener('focus', () => { editing = true; });
    // Commit on blur / Enter, not per keystroke - same "commit on release"
    // idea as the brightness slider's drag-end.
    const commit = () => {
        editing = false;
        const name = input.value.trim();
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'device_name', name: name }));
        }
        setRebootIndicator(true);
    };
    input.addEventListener('blur', commit);
    input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') input.blur();
    });

    document.getElementById('deviceNameRebootBtn').addEventListener('click', function () {
        this.textContent = 'Rebooting...';
        this.style.opacity = '0.6';
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'reboot' }));
        }
        setTimeout(() => location.reload(), 4000);
    });

    connectWebSocket();
});
