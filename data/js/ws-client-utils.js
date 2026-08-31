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

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { nextReconnectDelayMs };
}
