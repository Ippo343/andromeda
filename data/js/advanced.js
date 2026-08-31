// Advanced page wiring: pulls /metrics + the two rotated log files over plain
// HTTP every 2s and re-renders, and reuses the existing /ws WebSocket + state
// broadcast for the device-model selector (the same {type:'model'} /
// {type:'reboot'} commands controls.js sends). DOM/network glue only - the
// parsing and formatting live in advanced-logic.js so they can be unit-tested.

const REFRESH_MS = 2000;
let ws = null;
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

// Shows/hides the "needs a reboot to take effect" button next to the model
// row - driven both by a local change and by the server's rebootRequired flag.
function setRebootIndicator(show) {
    const row = document.getElementById('modelRow');
    const btn = document.getElementById('rebootBtn');
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
    if (msg.type !== 'state') return;

    const modelSelect = document.getElementById('modelSelect');
    if (modelSelect && Array.isArray(msg.models)) {
        // Only rebuild when the option count actually changed (mirrors controls.js's
        // effectSelect handling) - this page's state broadcasts arrive up to 10x/sec during
        // an unrelated live drag elsewhere (see comms.cpp's MIN_BROADCAST_INTERVAL_MS), and
        // unconditionally clearing+rebuilding on every one of them was closing an open
        // dropdown and overwriting a selection the user had just made, out from under them.
        if (modelSelect.options.length !== msg.models.length) {
            modelSelect.innerHTML = '';
            for (const model of msg.models) {
                const option = document.createElement('option');
                option.value = model.id;
                option.textContent = model.name;
                modelSelect.appendChild(option);
            }
        }
        if (msg.model && msg.model.configured && typeof msg.model.configured.id !== 'undefined') {
            modelSelect.value = String(msg.model.configured.id);
        }
    }
    if (msg.model) setRebootIndicator(msg.model.rebootRequired);
}

function renderMetrics(metrics) {
    const el = document.getElementById('metrics');
    const tiles = metricTiles(metrics);
    el.innerHTML = tiles
        .map((t) => `<div class="metric-tile">` +
            `<div class="metric-label">${escapeHtml(t.label)}</div>` +
            `<div class="metric-value">${escapeHtml(t.value)}</div></div>`)
        .join('');
}

function renderFirmware(metrics) {
    const el = document.getElementById('firmwareVersion');
    if (el) el.textContent = firmwareLabel(metrics);
}

function renderLogs(text) {
    const box = document.getElementById('logbox');
    const atBottom =
        box.scrollHeight - box.scrollTop - box.clientHeight < 40;

    const rows = text.split('\n').reduce((acc, line) => {
        if (line === '') return acc;
        const parsed = parseLogLine(line);
        if (parsed.raw !== undefined) {
            acc.push(`<div class="log-line log-raw">${escapeHtml(parsed.raw)}</div>`);
        } else {
            acc.push(`<div class="log-line ${parsed.levelClass}">` +
                `<span class="log-ts">${parsed.ts}</span>` +
                `<span class="log-pill">${escapeHtml(parsed.level)}</span>` +
                `${escapeHtml(parsed.message)}</div>`);
        }
        return acc;
    }, []);

    box.innerHTML = rows.length
        ? rows.join('')
        : '<div class="logbox-note">(log is empty)</div>';

    if (atBottom) box.scrollTop = box.scrollHeight;
}

// In-flight guards + timeouts for both polls below: without them, a device that's gone dark
// leaves each fetch queued behind the browser's own (much longer) connection timeout, and the
// next REFRESH_MS tick was free to fire another overlapping request on top - piling up
// exactly when the link is already struggling. Logs are the bigger cost here (re-fetching two
// full files every tick), so this also stops that pile-up from compounding.
let metricsRequestInFlight = false;
let logsRequestInFlight = false;
const FETCH_TIMEOUT_MS = 2000;

function fetchWithTimeout(url) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
    return fetch(url, { signal: controller.signal }).finally(() => clearTimeout(timeout));
}

function refresh() {
    if (!metricsRequestInFlight) {
        metricsRequestInFlight = true;
        fetchWithTimeout('/metrics')
            .then((r) => (r.ok ? r.json() : Promise.reject(new Error('not ok'))))
            .then((m) => {
                renderMetrics(m);
                renderFirmware(m);
            })
            .catch(() => {
                const el = document.getElementById('metrics');
                if (!el.children.length) {
                    el.innerHTML = '<div class="logbox-note">metrics unavailable</div>';
                }
            })
            .finally(() => { metricsRequestInFlight = false; });
    }

    if (!logsRequestInFlight) {
        logsRequestInFlight = true;
        // /log1.txt (rotated, older) then /log0.txt (current). Either can 404 -
        // before the first rotation there is no log1.txt, and log0.txt may not
        // exist for a moment at boot - treat a non-OK response as empty text.
        Promise.all([
            fetchWithTimeout('/log1.txt').then((r) => (r.ok ? r.text() : '')),
            fetchWithTimeout('/log0.txt').then((r) => (r.ok ? r.text() : '')),
        ])
            .then(([older, current]) => renderLogs(`${older}\n${current}`.trim()))
            .catch(() => {
                const box = document.getElementById('logbox');
                if (!box.textContent.trim()) {
                    box.innerHTML = '<div class="logbox-note">log unavailable</div>';
                }
            })
            .finally(() => { logsRequestInFlight = false; });
    }
}

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('logo').style.setProperty('--grad', randomGradient());

    document.getElementById('modelSelect').addEventListener('change', function () {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'model', id: parseInt(this.value, 10) }));
        }
        setRebootIndicator(true);
    });

    document.getElementById('rebootBtn').addEventListener('click', function () {
        this.textContent = 'Rebooting...';
        this.style.opacity = '0.6';
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'reboot' }));
        }
        setTimeout(() => location.reload(), 4000);
    });

    connectWebSocket();
    refresh();
    setInterval(refresh, REFRESH_MS);
});
