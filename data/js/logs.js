// Logs page wiring: pulls the two rotated log files over plain HTTP on a
// relaxed cadence (LOG_POLL_MS, only while the tab is visible and
// auto-refresh is on), skips re-rendering entirely when the fetched text is
// unchanged, and renders only the last MAX_LOG_LINES lines. DOM/network glue
// only - the parsing/formatting/tail logic lives in logs-logic.js so it can
// be unit-tested. Split out of advanced.js (#212): this page opens no
// WebSocket and shares none of the Advanced page's model/metrics/OTA state.

let autoRefreshOn = true;
let pollTimer = null;
let lastSignature = null;
let logsRequestInFlight = false;
// Line counts from the most recent render, so the pause toggle can refresh
// just the status text (auto-refresh on/off) without a network round trip.
let lastShownCount = 0;
let lastTotalCount = 0;
const FETCH_TIMEOUT_MS = 4000;

function fetchWithTimeout(url) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
    return fetch(url, { signal: controller.signal }).finally(() => clearTimeout(timeout));
}

function setStatus(text) {
    const el = document.getElementById('logStatus');
    if (el) el.textContent = text;
}

// One-shot fetch + (maybe) render. Called on load, by the Refresh button,
// on becoming visible, and by the auto-refresh timer - the in-flight guard
// just stops two of those triggers landing on top of each other; it doesn't
// gate whether a given call fetches at all.
function fetchLogs() {
    if (logsRequestInFlight) return;
    logsRequestInFlight = true;

    Promise.all([
        fetchWithTimeout('/log1.txt').then((r) => (r.ok ? r.text() : '')),
        fetchWithTimeout('/log0.txt').then((r) => (r.ok ? r.text() : '')),
    ])
        .then(([older, current]) => {
            const text = `${older}\n${current}`.trim();
            const sig = logSignature(text);
            if (sig === lastSignature) {
                // Unchanged since the last render - touch only the status
                // line (auto-refresh state may have changed) and skip all
                // DOM work on the log itself.
                setStatus(logStatusLabel(lastShownCount, lastTotalCount, autoRefreshOn));
                return;
            }
            lastSignature = sig;
            renderLogs(text);
        })
        .catch(() => {
            const box = document.getElementById('logbox');
            if (box && !box.textContent.trim()) {
                box.innerHTML = '<div class="logbox-note">log unavailable</div>';
            }
            setStatus('log unavailable');
        })
        .finally(() => { logsRequestInFlight = false; });
}

function renderLogs(text) {
    const box = document.getElementById('logbox');
    const atBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 40;

    const allLines = text ? text.split('\n').filter((l) => l !== '') : [];
    const shown = tailLines(text, MAX_LOG_LINES);

    const rows = shown.map((line) => {
        const parsed = parseLogLine(line);
        if (parsed.raw !== undefined) {
            return `<div class="log-line log-raw">${escapeHtml(parsed.raw)}</div>`;
        }
        return `<div class="log-line ${parsed.levelClass}">` +
            `<span class="log-ts">${parsed.ts}</span>` +
            `<span class="log-pill">${escapeHtml(parsed.level)}</span>` +
            `${escapeHtml(parsed.message)}</div>`;
    });

    box.innerHTML = rows.length
        ? rows.join('')
        : '<div class="logbox-note">(log is empty)</div>';

    if (atBottom) box.scrollTop = box.scrollHeight;

    lastShownCount = shown.length;
    lastTotalCount = allLines.length;
    setStatus(logStatusLabel(lastShownCount, lastTotalCount, autoRefreshOn));
}

function scheduleNextPoll() {
    if (pollTimer) clearTimeout(pollTimer);
    if (!autoRefreshOn || document.visibilityState === 'hidden') return;
    pollTimer = setTimeout(() => {
        fetchLogs();
        scheduleNextPoll();
    }, LOG_POLL_MS);
}

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('logo').style.setProperty('--grad', randomGradient());

    const autoToggle = document.getElementById('logAutoToggle');
    if (autoToggle) {
        autoToggle.checked = autoRefreshOn;
        autoToggle.addEventListener('change', function () {
            autoRefreshOn = this.checked;
            if (autoRefreshOn) {
                fetchLogs();
                scheduleNextPoll();
            } else {
                if (pollTimer) clearTimeout(pollTimer);
                pollTimer = null;
                // No new fetch - just correct the "auto-refreshing" claim in
                // the status line against what's already on screen.
                setStatus(logStatusLabel(lastShownCount, lastTotalCount, false));
            }
        });
    }

    const refreshBtn = document.getElementById('logRefreshBtn');
    if (refreshBtn) {
        refreshBtn.addEventListener('click', () => fetchLogs());
    }

    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') {
            fetchLogs();
            scheduleNextPoll();
        } else if (pollTimer) {
            clearTimeout(pollTimer);
            pollTimer = null;
        }
    });

    fetchLogs();
    scheduleNextPoll();
});
