const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const { nextReconnectDelayMs, shouldRebuildOptions } = require('../../data/js/ws-client-utils.js');

// Minimal stand-in for an HTMLOptionElement / the option array a rebuild
// produces.
const opt = (value, textContent) => ({ value: String(value), textContent });

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

describe('shouldRebuildOptions', () => {
    const models = [
        { id: 512, name: 'Andromeda MK0' },
        { id: 768, name: 'L70 MK1' },
        { id: 770, name: 'L10 MK1' },
    ];

    test('no rebuild when the rendered options already match id and label', () => {
        const rendered = models.map((m) => opt(m.id, m.name));
        assert.equal(shouldRebuildOptions(rendered, models), false);
    });

    test('rebuild when a label differs but the count is identical', () => {
        // The exact Advanced-page bug: 5 placeholder options, 5 real models,
        // wrong names. Same length, different content.
        const stale = [
            opt(512, 'Andromeda'),   // firmware name is "Andromeda MK0"
            opt(768, 'L70 MK1'),
            opt(770, 'L10 MK1'),
        ];
        assert.equal(shouldRebuildOptions(stale, models), true);
    });

    test('rebuild when an id was swapped out (count unchanged)', () => {
        const swapped = [
            opt(512, 'Andromeda MK0'),
            opt(999, 'H1 Prototype'),  // 768 replaced
            opt(770, 'L10 MK1'),
        ];
        assert.equal(shouldRebuildOptions(swapped, models), true);
    });

    test('rebuild from an empty select', () => {
        assert.equal(shouldRebuildOptions([], models), true);
    });

    test('non-array server list -> no rebuild (nothing to build from)', () => {
        assert.equal(shouldRebuildOptions([opt(1, 'x')], undefined), false);
        assert.equal(shouldRebuildOptions([opt(1, 'x')], null), false);
    });

    test('numeric server ids and string option values compare equal', () => {
        assert.equal(shouldRebuildOptions([opt('770', 'L10 MK1')],
            [{ id: 770, name: 'L10 MK1' }]), false);
    });
});
