// Pure, DOM-free WS-reconnect helper shared by controls.js/advanced.js/device-name.js - each
// page runs its own connectWebSocket()/handleStateMessage() (there's no bundler here, so
// nothing to share the DOM wiring itself through), but the backoff math is identical across
// all three and worth keeping in one place and one test.

// Jittered exponential backoff for a WS reconnect attempt (0-indexed), capped at 30s. A fixed
// 2s retry previously kept every open tab hammering a device that's simply off, in lockstep
// with every other tab.
function nextReconnectDelayMs(attempt, randomFn = Math.random) {
    const base = Math.min(30000, 1000 * Math.pow(2, attempt));
    return Math.round(base * (0.5 + randomFn() * 0.5));
}

// Whether a <select>'s rendered <option>s no longer match the server's list
// and it needs rebuilding. Compares the full (id, label) content, not just the
// count. The count-only guard this replaces silently failed on the Advanced
// page: advanced.html shipped a placeholder list the same length as the
// firmware's model registry, so the branch never ran - the user saw stale
// placeholder labels, and a future model swap that kept the count would have
// left the dropdown offering an id the firmware no longer knew (a soft-brick,
// exactly what data/index.html warns about).
//
// `currentOptions` is the live HTMLOptionsCollection (or any array-like of
// { value, textContent }); `serverList` is the [{ id, name }] from the state
// message. Non-array serverList -> false (nothing to rebuild from).
function shouldRebuildOptions(currentOptions, serverList) {
    if (!Array.isArray(serverList)) return false;
    const rendered = Array.from(currentOptions || [])
        .map((o) => [String(o.value), String(o.textContent)]);
    const wanted = serverList.map((x) => [String(x.id), String(x.name)]);
    return JSON.stringify(rendered) !== JSON.stringify(wanted);
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { nextReconnectDelayMs, shouldRebuildOptions };
}
