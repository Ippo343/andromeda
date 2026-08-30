// Device-name page wiring. Deliberately on its own page, off the main
// controls page: it holds the only free-text <input> in the UI, and iOS's
// captive-portal webview autofocuses the first text field of whatever page it
// loads (always "/") and zooms in. Keeping it here means "/" has no text
// input to grab. Uses the existing /ws WebSocket + state broadcast - the same
// {type:'device_name'} / {type:'reboot'} commands controls.js used to send.

let ws = null;
let editing = false;

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}/ws`);
    ws.onopen = () => document.body.classList.remove('ws-disconnected');
    ws.onclose = () => {
        document.body.classList.add('ws-disconnected');
        setTimeout(connectWebSocket, 2000);
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

    const input = document.getElementById('deviceNameInput');
    if (input && !editing) input.value = msg.device.name;

    const hint = document.getElementById('deviceUidHint');
    if (hint) hint.textContent = `Device ID: ${msg.device.uid}`;

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
