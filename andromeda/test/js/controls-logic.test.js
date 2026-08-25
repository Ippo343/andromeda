const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    pickInitialTab,
    findEffectIdByName,
    holdIcon,
    rgbToHistogramWidths,
    hsvToRgb,
} = require('../../data/js/controls-logic.js');

describe('pickInitialTab', () => {
    test('returns "color" when color is active', () => {
        assert.equal(pickInitialTab({ color: { active: true } }), 'color');
    });

    test('returns "random" when color is not active', () => {
        assert.equal(pickInitialTab({ color: { active: false } }), 'random');
    });

    test('returns "random" when color is missing', () => {
        assert.equal(pickInitialTab({}), 'random');
    });
});

describe('findEffectIdByName', () => {
    const effects = [
        { id: 0, name: 'CartesianMoodlight' },
        { id: 5, name: 'NinjaStar' },
    ];

    test('finds the id of a matching effect', () => {
        assert.equal(findEffectIdByName(effects, 'NinjaStar'), 5);
    });

    test('returns null when no effect matches', () => {
        assert.equal(findEffectIdByName(effects, 'Unknown'), null);
    });

    test('returns null when effects is not an array', () => {
        assert.equal(findEffectIdByName(undefined, 'NinjaStar'), null);
    });
});

describe('holdIcon', () => {
    test('shows pause and sends hold when not holding', () => {
        assert.deepEqual(holdIcon(false), { icon: 'pause', nextCmd: 'hold' });
    });

    test('shows play and sends resume when holding', () => {
        assert.deepEqual(holdIcon(true), { icon: 'play', nextCmd: 'resume' });
    });
});

describe('rgbToHistogramWidths', () => {
    test('converts 0-255 channel values to percentages', () => {
        const widths = rgbToHistogramWidths(0, 128, 255);
        assert.equal(widths.r, 0);
        assert.ok(Math.abs(widths.g - 50.196) < 0.01);
        assert.equal(widths.b, 100);
    });
});

describe('hsvToRgb', () => {
    test('h=0 s=1 v=1 is pure red', () => {
        assert.deepEqual(hsvToRgb(0, 1, 1), [255, 0, 0]);
    });

    test('h=120 s=1 v=1 is pure green', () => {
        assert.deepEqual(hsvToRgb(120, 1, 1), [0, 255, 0]);
    });

    test('h=240 s=1 v=1 is pure blue', () => {
        assert.deepEqual(hsvToRgb(240, 1, 1), [0, 0, 255]);
    });

    test('s=0 is white regardless of hue', () => {
        assert.deepEqual(hsvToRgb(90, 0, 1), [255, 255, 255]);
    });

    test('v=0 is black regardless of hue/saturation', () => {
        assert.deepEqual(hsvToRgb(90, 1, 0), [0, 0, 0]);
    });
});
