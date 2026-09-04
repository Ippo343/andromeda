// Pure, DOM-free logic for the standalone Logs page, pulled out of
// advanced-logic.js (#212) so it's testable under Node (node --test test/js)
// without a browser - same pattern as controls-logic.js / ws-state-builder.h.
// Loaded via a plain <script> before logs.js (global scope, no bundler), and
// required directly by tests via the module.exports guard at the bottom.

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
// prefix lambda's "UNK" fallback) to the CSS class logs.css colours.
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

// How often logs.js auto-refreshes while the page is visible and auto-refresh
// is on, and how many of the most recent lines it renders. Both deliberately
// relaxed vs. the old 1s/unbounded Advanced-page poll (#212/#214): this page
// is opened specifically to read logs, so a slower cadence and a capped tail
// are the right tradeoff, not a regression - and a Refresh button + a raw-file
// link cover the "I need more/fresher than that" case.
const LOG_POLL_MS = 5000;
const MAX_LOG_LINES = 500;

// Cheap fingerprint of a fetched log blob, used to skip re-rendering when
// nothing changed. Length + a tail slice is enough to detect the common
// case (new lines appended, or nothing appended) without retaining a full
// second copy of a many-KB string just to compare it.
function logSignature(text) {
    const s = text || '';
    return `${s.length}:${s.slice(-96)}`;
}

// Last `maxLines` non-empty lines of `text`, in original order. Fewer lines
// than the cap -> all of them; blank text -> [].
function tailLines(text, maxLines) {
    const lines = (text || '').split('\n').filter((l) => l !== '');
    if (lines.length <= maxLines) return lines;
    return lines.slice(lines.length - maxLines);
}

// Status line under the toolbar: how much of the log is shown, and whether
// auto-refresh is currently on. `shown`/`total` are line counts.
function logStatusLabel(shown, total, autoOn) {
    const countPart = total > shown
        ? `showing the last ${shown} of ${total} lines`
        : `showing all ${total} line${total === 1 ? '' : 's'}`;
    return autoOn ? `${countPart} · auto-refreshing every 5s` : `${countPart} · auto-refresh paused`;
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        parseLogLine,
        levelClass,
        LOG_POLL_MS,
        MAX_LOG_LINES,
        logSignature,
        tailLines,
        logStatusLabel,
    };
}
