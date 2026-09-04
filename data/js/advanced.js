// Advanced page wiring: pulls /metrics over plain HTTP every 1s and
// re-renders, and reuses the existing /ws WebSocket + state broadcast for the
// device-model selector (the same {type:'model'} / {type:'reboot'} commands
// controls.js sends). DOM/network glue only - the parsing and formatting
// live in advanced-logic.js so they can be unit-tested. The log viewer moved
// to its own page (data/logs.html, #212) - see that page's logs.js instead.

const REFRESH_MS = 1000;
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
        // Rebuild only when the list's contents actually differ from what's
        // rendered - this page's state broadcasts arrive up to 10x/sec during
        // an unrelated live drag elsewhere (see comms.cpp's
        // MIN_BROADCAST_INTERVAL_MS), and clearing+rebuilding on every one
        // closed an open dropdown / overwrote a just-made selection. A
        // length-only check (the old guard) never fired at all, because the
        // hard-coded fallback happened to have the same length as the model
        // registry.
        if (shouldRebuildOptions(modelSelect.options, msg.models)) {
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

// Cached {labelEl, valueEl} per tile index from the last renderMetrics() call
// that actually built the DOM, so a same-shaped update (the common case -
// metricTiles() always returns the same label set) only touches the
// .metric-value text nodes that changed instead of rebuilding the whole
// #metrics grid every tick (#214). Rebuilds from scratch only when the tile
// count itself changes (effectively never - metricTiles()'s label list is
// static - but cheap insurance if it ever does).
let metricTileEls = [];

function renderMetrics(metrics) {
    const el = document.getElementById('metrics');
    const tiles = metricTiles(metrics);

    if (metricTileEls.length !== tiles.length) {
        el.innerHTML = tiles
            .map(() => '<div class="metric-tile">' +
                '<div class="metric-label"></div>' +
                '<div class="metric-value"></div></div>')
            .join('');
        const nodes = el.children;
        metricTileEls = tiles.map((_, i) => ({
            labelEl: nodes[i].querySelector('.metric-label'),
            valueEl: nodes[i].querySelector('.metric-value'),
        }));
    }

    tiles.forEach((t, i) => {
        const { labelEl, valueEl } = metricTileEls[i];
        if (labelEl.textContent !== t.label) labelEl.textContent = t.label;
        if (valueEl.textContent !== t.value) valueEl.textContent = t.value;
    });
}

function renderFirmware(metrics) {
    const el = document.getElementById('firmwareVersion');
    if (el) el.textContent = firmwareLabel(metrics);
}

// True while an OTA action is running, so the 2s /metrics refresh doesn't
// yank the checkbox back or hide the progress line under an in-flight update.
let otaBusy = false;

// Driven by the /metrics poll: the "update available" badge, the "Update now"
// button, and the dev-channel checkbox (unless the user is mid-interaction).
function renderOta(metrics) {
    const badge = document.getElementById('fwUpdateBadge');
    const updateBtn = document.getElementById('otaUpdateBtn');
    const text = otaBadgeText(metrics);
    if (badge) {
        badge.textContent = text;
        badge.hidden = !text;
    }
    if (updateBtn && !otaBusy) updateBtn.hidden = !text;

    const devBox = document.getElementById('otaDevChannel');
    if (devBox && !otaBusy) devBox.checked = metrics && metrics.otaChannel === 'dev';
}

// Polls /ota-status ~1x/s and reflects it into #otaProgress, mirroring
// wifi.js's pollSaveStatus. Stops on a terminal state; on "rebooting" it
// counts down and reloads, like the reboot button. `duringUpdate` (set by
// "Update now") keeps the loop alive through the brief window where the
// device still reports its pre-trigger state - see isOtaPollDone().
function pollOtaStatus(duringUpdate) {
    const progress = document.getElementById('otaProgress');
    let rebootCountdown = 90;

    function poll() {
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 5000);

        fetch('/ota-status', { signal: controller.signal })
            .finally(() => clearTimeout(timeout))
            .then((r) => (r.ok ? r.json() : Promise.reject(new Error('status ' + r.status))))
            .then((s) => {
                const label = otaProgressLabel(s);
                if (progress) {
                    progress.textContent = label;
                    progress.hidden = !label;
                }

                if (s.state === 'rebooting') {
                    const tick = () => {
                        if (progress) {
                            progress.textContent =
                                `Rebooting into the new firmware… reloading in ${rebootCountdown}s`;
                        }
                        if (rebootCountdown-- <= 0) { location.reload(); return; }
                        setTimeout(tick, 1000);
                    };
                    tick();
                    return;
                }

                if (isOtaPollDone(s.state, duringUpdate)) {
                    otaBusy = false;
                    refresh();  // repaint badge / button / checkbox from fresh /metrics
                    return;
                }
                setTimeout(poll, 1000);
            })
            .catch(() => {
                // A dropped poll during the flash is expected (radio busy). Keep
                // trying - the device either comes back or reboots under us.
                setTimeout(poll, 1500);
            });
    }

    poll();
}

// In-flight guard + timeout: without it, a device that's gone dark leaves the fetch queued
// behind the browser's own (much longer) connection timeout, and the next REFRESH_MS tick was
// free to fire another overlapping request on top - piling up exactly when the link is already
// struggling.
let metricsRequestInFlight = false;
const FETCH_TIMEOUT_MS = 2000;

function fetchWithTimeout(url) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
    return fetch(url, { signal: controller.signal }).finally(() => clearTimeout(timeout));
}

function refresh() {
    if (metricsRequestInFlight) return;
    metricsRequestInFlight = true;
    fetchWithTimeout('/metrics')
        .then((r) => (r.ok ? r.json() : Promise.reject(new Error('not ok'))))
        .then((m) => {
            renderMetrics(m);
            renderFirmware(m);
            renderOta(m);
        })
        .catch(() => {
            const el = document.getElementById('metrics');
            if (!el.children.length) {
                el.innerHTML = '<div class="metrics-note">metrics unavailable</div>';
            }
        })
        .finally(() => { metricsRequestInFlight = false; });
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

    // Show a start-request rejection (409/503 from OtaStartGate) in the
    // progress line and undo the "busy" UI, so a click that spawned no worker
    // never leaves the buttons stuck disabled behind a poll that never moves.
    function handleOtaStart(response, buttons, duringUpdate) {
        const progress = document.getElementById('otaProgress');
        if (response.ok) { pollOtaStatus(duringUpdate); return; }
        return response.text().then((body) => {
            otaBusy = false;
            buttons.forEach((b) => { if (b) b.disabled = false; });
            if (progress) {
                progress.textContent = otaStartRejectionLabel(response.status, body);
                progress.hidden = false;
            }
        });
    }

    document.getElementById('otaCheckBtn').addEventListener('click', () => {
        const checkBtn = document.getElementById('otaCheckBtn');
        otaBusy = true;
        const progress = document.getElementById('otaProgress');
        if (progress) { progress.textContent = 'Checking for updates…'; progress.hidden = false; }
        fetch('/ota-check', { method: 'POST' })
            .then((r) => handleOtaStart(r, [checkBtn], false))
            // Network dropped issuing the POST - the check may still be running.
            .catch(() => pollOtaStatus(false));
    });

    document.getElementById('otaUpdateBtn').addEventListener('click', () => {
        if (!confirm('Download and install the new firmware now? The device will reboot.')) return;
        const updateBtn = document.getElementById('otaUpdateBtn');
        const checkBtn = document.getElementById('otaCheckBtn');
        otaBusy = true;
        updateBtn.disabled = true;
        checkBtn.disabled = true;
        const progress = document.getElementById('otaProgress');
        if (progress) { progress.textContent = 'Starting update…'; progress.hidden = false; }
        fetch('/ota', { method: 'POST' })
            .then((r) => handleOtaStart(r, [updateBtn, checkBtn], true))
            // Network dropped issuing the POST - the device may still have
            // started; fall back to polling.
            .catch(() => pollOtaStatus(true));
    });

    document.getElementById('otaDevChannel').addEventListener('change', function () {
        otaBusy = true;
        fetch('/ota-channel', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'dev=' + (this.checked ? 'true' : 'false'),
        })
            .catch(() => {})
            .finally(() => { otaBusy = false; refresh(); });
    });

    connectWebSocket();
    refresh();
    setInterval(refresh, REFRESH_MS);
});
