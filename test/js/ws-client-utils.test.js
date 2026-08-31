const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const { nextReconnectDelayMs } = require('../../data/js/ws-client-utils.js');

describe('nextReconnectDelayMs', () => {
    test('attempt 0 is roughly the base 1s, jittered between 50%-100%', () => {
        const delay = nextReconnectDelayMs(0, () => 0);
        assert.equal(delay, 500);  // 0.5 + 0*0.5 = 0.5 of 1000ms
        const delayMax = nextReconnectDelayMs(0, () => 1);
        assert.equal(delayMax, 1000);  // 0.5 + 1*0.5 = 1.0 of 1000ms
    });

    test('doubles per attempt', () => {
        assert.equal(nextReconnectDelayMs(1, () => 1), 2000);
        assert.equal(nextReconnectDelayMs(2, () => 1), 4000);
        assert.equal(nextReconnectDelayMs(3, () => 1), 8000);
    });

    test('caps at 30s regardless of how large the attempt number gets', () => {
        assert.equal(nextReconnectDelayMs(10, () => 1), 30000);
        assert.equal(nextReconnectDelayMs(100, () => 1), 30000);
    });

    test('jitter never produces a delay below half the base', () => {
        for (let attempt = 0; attempt < 6; attempt++) {
            const base = Math.min(30000, 1000 * Math.pow(2, attempt));
            const delay = nextReconnectDelayMs(attempt, () => 0);
            assert.equal(delay, Math.round(base * 0.5));
        }
    });
});
