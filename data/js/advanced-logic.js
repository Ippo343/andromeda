// Pure, DOM-free logic for the Advanced page (metrics + log viewer), pulled
// out of advanced.js so it's testable under Node (node --test test/js)
// without a browser - same pattern as controls-logic.js / ws-state-builder.h.
// Loaded via a plain <script> before advanced.js (global scope, no bundler),
// and required directly by tests via the module.exports guard at the bottom.

// A firmware log line is "<millis> | <LVL> | <message>" (see the Log.setPrefix
// lambda in include/loggers.h). Parse one line into its parts; anything that
// doesn't match that shape (blank lines, wrapped continuations) comes back as
// { raw } so the caller can still render it, just without a level pill.
function parseLogLine(line) {
    const m = /^(\d+)\s*\|\s*([A-Za-z]{2,4})\s*\|\s*([\s\S]*)$/.exec(line);
    if (!m) {
        return { raw: line, levelClass: 'log-raw' };
    }
    const level = m[2].toUpperCase();
    return {
        ts: Number(m[1]),
        level: level,
        levelClass: levelClass(level),
        message: m[3],
    };
}

// Maps a 3-letter ArduinoLog level tag (FTL/ERR/WRN/INF/TRC/VRB, plus the
// prefix lambda's "UNK" fallback) to the CSS class advanced.css colours.
function levelClass(level) {
    switch ((level || '').toUpperCase()) {
        case 'FTL': return 'log-ftl';
        case 'ERR': return 'log-err';
        case 'WRN': return 'log-wrn';
        case 'INF': return 'log-inf';
        case 'TRC': return 'log-trc';
        case 'VRB': return 'log-vrb';
        default: return 'log-raw';
    }
}

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

if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        parseLogLine,
        levelClass,
        formatUptime,
        formatBytes,
        resetReasonLabel,
        formatTemp,
        metricTiles,
        firmwareLabel,
    };
}
