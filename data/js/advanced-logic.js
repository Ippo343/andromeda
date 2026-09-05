// Pure, DOM-free logic for the Advanced page (metrics + model/firmware/OTA),
// pulled out of advanced.js so it's testable under Node (node --test test/js)
// without a browser - same pattern as controls-logic.js / ws-state-builder.h.
// Loaded via a plain <script> before advanced.js (global scope, no bundler),
// and required directly by tests via the module.exports guard at the bottom.
// The log viewer's own logic (parseLogLine/levelClass) moved to
// logs-logic.js with the page itself (#212).

// millis-since-boot -> "1d 03h 12m 05s" (the "Nd " part is dropped when the
// device has been up less than a day). Non-finite / negative input -> "-".
function formatUptime(ms) {
    if (!Number.isFinite(ms) || ms < 0) return '-';
    let s = Math.floor(ms / 1000);
    const d = Math.floor(s / 86400); s -= d * 86400;
    const h = Math.floor(s / 3600); s -= h * 3600;
    const m = Math.floor(s / 60); s -= m * 60;
    const pad = (n) => String(n).padStart(2, '0');
    const tail = `${pad(h)}h ${pad(m)}m ${pad(s)}s`;
    return d > 0 ? `${d}d ${tail}` : tail;
}

// Byte count -> short human string ("512 B", "12.3 KB", "1.4 MB"). Non-finite
// input -> "-".
function formatBytes(n) {
    if (!Number.isFinite(n)) return '-';
    if (n < 1024) return `${n} B`;
    if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
    return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}

// esp_reset_reason() enum value -> short label. Unknown codes fall through to
// "reset #<n>" rather than throwing.
function resetReasonLabel(code) {
    const map = {
        0: 'unknown',
        1: 'power-on',
        2: 'external',
        3: 'software',
        4: 'panic',
        5: 'interrupt watchdog',
        6: 'task watchdog',
        7: 'other watchdog',
        8: 'deep-sleep wake',
        9: 'brownout',
        10: 'SDIO',
    };
    return Object.prototype.hasOwnProperty.call(map, code) ? map[code] : `reset #${code}`;
}

function formatTemp(c) {
    return Number.isFinite(c) ? `${c.toFixed(1)} °C (approx)` : '-';
}

// Estimated current draw vs. the configured budget (ModelConfig::max_milliamps)
// -> "234 / 9000 mA". Either value missing/non-finite -> '-' (matches the
// other tiles' null-guard style).
function formatCurrentDraw(currentMa, maxMilliamps) {
    if (!Number.isFinite(currentMa) || !Number.isFinite(maxMilliamps)) return '-';
    return `${Math.round(currentMa)} / ${Math.round(maxMilliamps)} mA`;
}

// The slider ceiling the device computed at boot from its power budget (#237)
// -> "152 / 255". Missing/non-finite -> '-'.
function formatBrightnessCeiling(brightnessCeiling) {
    if (!Number.isFinite(brightnessCeiling)) return '-';
    return `${Math.round(brightnessCeiling)} / 255`;
}

// Turns the /metrics JSON payload into the ordered [{ label, value }] list the
// Advanced page renders as tiles. Keeping it here (not in the DOM code) makes
// the formatting testable. A missing payload yields an empty list.
function metricTiles(m) {
    if (!m || typeof m !== 'object') return [];
    return [
        { label: 'Uptime', value: formatUptime(m.uptimeMs) },
        { label: 'Free heap', value: formatBytes(m.heapFree) },
        { label: 'Min free heap', value: formatBytes(m.heapMin) },
        { label: 'Heap total', value: formatBytes(m.heapTotal) },
        { label: 'Temperature', value: formatTemp(m.tempC) },
        { label: 'FPS', value: m.fps == null ? '-' : String(Math.round(m.fps)) },
        { label: 'WiFi RSSI', value: m.rssi == null ? '-' : `${m.rssi} dBm` },
        { label: 'CPU', value: m.cpuMhz == null ? '-' : `${m.cpuMhz} MHz` },
        { label: 'Current draw', value: formatCurrentDraw(m.currentMa, m.maxMilliamps) },
        { label: 'Brightness cap', value: formatBrightnessCeiling(m.brightnessCeiling) },
        { label: 'Chip', value: m.chip || '-' },
        { label: 'Last reset', value: resetReasonLabel(m.resetReason) },
    ];
}

// The build-time git-describe string from /metrics, or "-" when absent. Shown
// in its own box on the Advanced page rather than as a metrics tile - it's far
// longer than any other value and looked lopsided in the 2-column grid.
function firmwareLabel(m) {
    return (m && m.version) || '-';
}

// --- OTA (#63) ------------------------------------------------------------

// Monotonic version-code compare. Non-numeric / missing -> not newer, so a
// bad payload never nags for an update.
function isNewer(localCode, remoteCode) {
    const a = Number(localCode);
    const b = Number(remoteCode);
    if (!Number.isFinite(a) || !Number.isFinite(b)) return false;
    return b > a;
}

// Badge text for the Firmware section from /metrics. "" when nothing's
// available (the caller hides the badge), otherwise "<tag> available".
function otaBadgeText(m) {
    if (!m || !m.updateAvailable) return '';
    const tag = (m.latestTag || '').trim();
    return tag ? `${tag} available` : 'update available';
}

// /ota-status payload -> a short line for the progress area. Returns "" for
// states with nothing to show (idle / a completed check), so the caller can
// blank and hide the element.
function otaProgressLabel(s) {
    if (!s || typeof s !== 'object') return '';
    const pct = Number.isFinite(Number(s.progress)) ? Number(s.progress) : 0;
    switch (s.state) {
        case 'checking': return 'Checking for updates…';
        case 'downloading': return `Downloading… ${pct}%`;
        case 'writing-fw': return `Writing firmware… ${pct}%`;
        case 'writing-fs': return `Writing filesystem… ${pct}%`;
        case 'rebooting': return 'Rebooting into the new firmware…';
        case 'failed': return `Update failed: ${s.error || 'unknown error'}`;
        default: return '';  // idle / uptodate / available
    }
}

// A failed POST /ota or /ota-check now comes back non-2xx with a plain-text
// reason (see OtaStartGate in the firmware) instead of a lying 202. Turn that
// into a line for #otaProgress. `bodyText` is the response body; `status` the
// HTTP code as a fallback when the body is empty.
function otaStartRejectionLabel(status, bodyText) {
    const reason = (bodyText || '').trim() || ('HTTP ' + status);
    return `Couldn't start: ${reason}`;
}

// States where the poll loop should stop (nothing more will change without a
// new user action, or the device is about to reboot out from under us).
function isOtaTerminalState(state) {
    return state === 'idle' || state === 'uptodate' || state === 'available' ||
        state === 'failed' || state === 'rebooting';
}

// When the /ota-status poll loop should stop. `duringUpdate` is true for the
// loop started by "Update now": right after the POST the device still reports
// its pre-trigger state ('available', or a stale 'idle') for a few ms until
// the worker task flips it, and treating those as terminal there killed the
// loop before the first progress frame ever rendered. During an update only a
// real end state counts ('rebooting' is handled by the caller before this).
function isOtaPollDone(state, duringUpdate) {
    if (duringUpdate) return state === 'uptodate' || state === 'failed';
    return isOtaTerminalState(state);
}

// --- Metrics over /ws (#214) -----------------------------------------------

// Exact strings WsCommandParser::parseSubscription() (ws-command-parser.h) matches - kept as
// functions (not a shared constant) so a test can assert on the literal wire text without
// reaching into advanced.js's WebSocket plumbing.
function subscribeMetricsMessage() {
    return JSON.stringify({ type: 'subscribe', topic: 'metrics' });
}
function unsubscribeMetricsMessage() {
    return JSON.stringify({ type: 'unsubscribe', topic: 'metrics' });
}

// Folds one {"type":"metrics",...} WS frame into the running cache. Frames are tiered
// (WsMetricsBuilder - see ws-metrics-builder.h): a volatile-only frame must not blank the
// static (chip/version/...) or OTA fields a previous full frame already populated, so this is
// a shallow merge, not a replace. `type` itself is dropped - the merged object is meant to be
// handed straight to metricTiles()/firmwareLabel()/otaBadgeText(), which know nothing about
// WS framing and expect the same flat shape /metrics returns. A null/non-object frame (a
// malformed message JSON.parse still let through) is a no-op, returning `prev` unchanged.
function mergeMetrics(prev, frame) {
    if (!frame || typeof frame !== 'object') return prev || {};
    const merged = Object.assign({}, prev || {}, frame);
    delete merged.type;
    return merged;
}

// How long to wait for the first/next WS metrics frame before falling back to polling
// /metrics over HTTP - covers a device that hasn't (yet) rebooted into WS-push-capable
// firmware, or a subscription that silently didn't take.
const WS_METRICS_GRACE_MS = 3000;
// Fallback HTTP poll cadence once the grace window has been missed - far slower than the old
// unconditional 1s poll, since on a healthy device this path is never taken at all.
const METRICS_HTTP_FALLBACK_MS = 5000;

// True when advanced.js's fallback timer should fire a one-shot HTTP /metrics fetch this tick:
// the socket isn't open at all, or it is open but no metrics frame has arrived within the
// grace window (a stale subscription, or older firmware with no WS metrics push).
function shouldPollMetricsHttp(wsOpen, lastFrameAtMs, nowMs) {
    if (!wsOpen) return true;
    if (lastFrameAtMs == null) return true;
    return (nowMs - lastFrameAtMs) >= WS_METRICS_GRACE_MS;
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        formatUptime,
        formatBytes,
        resetReasonLabel,
        formatTemp,
        formatCurrentDraw,
        formatBrightnessCeiling,
        metricTiles,
        firmwareLabel,
        isNewer,
        otaBadgeText,
        otaProgressLabel,
        otaStartRejectionLabel,
        isOtaTerminalState,
        isOtaPollDone,
        subscribeMetricsMessage,
        unsubscribeMetricsMessage,
        mergeMetrics,
        shouldPollMetricsHttp,
        WS_METRICS_GRACE_MS,
        METRICS_HTTP_FALLBACK_MS,
    };
}
