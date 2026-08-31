const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    pickInitialTab,
    findEffectIdByName,
    holdIcon,
    powerButtonState,
    rgbToHistogramWidths,
    hsvToRgb,
    rgbToHsv,
    hsToWheelPosition,
    logoBrightnessFilter,
    sanitizeStateMessage,
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
        { id: 0, name: 'Cartesian Moodlight' },
        { id: 5, name: 'Ninja Star' },
    ];

    test('finds the id of a matching effect', () => {
        assert.equal(findEffectIdByName(effects, 'Ninja Star'), 5);
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

describe('powerButtonState', () => {
    test('when on, next click turns it off', () => {
        assert.deepEqual(powerButtonState(true), {
            nextCmd: 'power_off',
            cssClass: 'off',
            label: 'Turn off',
        });
    });

    test('when off, next click turns it on', () => {
        assert.deepEqual(powerButtonState(false), {
            nextCmd: 'power_on',
            cssClass: 'on',
            label: 'Turn on',
        });
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

    test('h=360 wraps to the same result as h=0 (not grey)', () => {
        // The if-chain only covers [0, 360) - 360 previously fell through every branch and
        // silently returned grey (r=g=b=m) instead of wrapping back to red.
        assert.deepEqual(hsvToRgb(360, 1, 1), hsvToRgb(0, 1, 1));
    });

    test('negative h wraps into range', () => {
        assert.deepEqual(hsvToRgb(-120, 1, 1), hsvToRgb(240, 1, 1));
    });

    test('h well past 360 wraps via modulo', () => {
        assert.deepEqual(hsvToRgb(720 + 120, 1, 1), hsvToRgb(120, 1, 1));
    });
});

describe('rgbToHsv', () => {
    test('pure red', () => {
        const { h, s, v } = rgbToHsv(255, 0, 0);
        assert.equal(h, 0);
        assert.equal(s, 1);
        assert.equal(v, 1);
    });

    test('pure green', () => {
        const { h, s, v } = rgbToHsv(0, 255, 0);
        assert.equal(h, 120);
        assert.equal(s, 1);
        assert.equal(v, 1);
    });

    test('pure blue', () => {
        const { h, s, v } = rgbToHsv(0, 0, 255);
        assert.equal(h, 240);
        assert.equal(s, 1);
        assert.equal(v, 1);
    });

    test('white has zero saturation (hue is irrelevant/0)', () => {
        const { h, s, v } = rgbToHsv(255, 255, 255);
        assert.equal(h, 0);
        assert.equal(s, 0);
        assert.equal(v, 1);
    });

    test('black has zero value', () => {
        const { v } = rgbToHsv(0, 0, 0);
        assert.equal(v, 0);
    });

    test('round-trips through hsvToRgb for an arbitrary color', () => {
        const [r, g, b] = hsvToRgb(200, 0.6, 0.9);
        const { h, s, v } = rgbToHsv(r, g, b);
        // hsvToRgb rounds to byte values, so the round trip is only exact to
        // within the resulting rounding error, not bit-for-bit.
        assert.ok(Math.abs(h - 200) < 2);
        assert.ok(Math.abs(s - 0.6) < 0.02);
        assert.ok(Math.abs(v - 0.9) < 0.02);
    });
});

describe('hsToWheelPosition', () => {
    test('zero saturation lands exactly on center regardless of hue', () => {
        assert.deepEqual(hsToWheelPosition(90, 0, 50, 50, 40), { x: 50, y: 50 });
    });

    test('full saturation at hue=0 lands on the +x rim', () => {
        const { x, y } = hsToWheelPosition(0, 1, 50, 50, 40);
        assert.ok(Math.abs(x - 90) < 1e-9);
        assert.ok(Math.abs(y - 50) < 1e-9);
    });

    test('saturation is clamped into [0, 1]', () => {
        const over = hsToWheelPosition(0, 5, 50, 50, 40);
        const atOne = hsToWheelPosition(0, 1, 50, 50, 40);
        assert.deepEqual(over, atOne);
    });

    test('round-trips with the hue/saturation ColorWheel.updateColor() would derive from the same (x, y)', () => {
        // Mirrors updateColor()'s own math (angle = atan2(dy, dx), hue = angle in
        // degrees normalized to [0, 360), saturation = distance / radius) without
        // importing the DOM-bound ColorWheel class itself.
        const centerX = 50, centerY = 50, radius = 40;
        const hue = 217, saturation = 0.42;
        const { x, y } = hsToWheelPosition(hue, saturation, centerX, centerY, radius);

        const dx = x - centerX, dy = y - centerY;
        const distance = Math.sqrt(dx * dx + dy * dy);
        const angle = Math.atan2(dy, dx);
        const derivedHue = (angle * 180 / Math.PI + 360) % 360;
        const derivedSaturation = distance / radius;

        assert.ok(Math.abs(derivedHue - hue) < 1e-9);
        assert.ok(Math.abs(derivedSaturation - saturation) < 1e-9);
    });
});

describe('sanitizeStateMessage', () => {
    test('passes through well-formed fields', () => {
        assert.deepEqual(sanitizeStateMessage({ power: true, holding: false, brightness: 128 }),
            { power: true, holding: false, brightness: 128 });
    });

    test('missing power/holding default to false', () => {
        assert.deepEqual(sanitizeStateMessage({ brightness: 10 }),
            { power: false, holding: false, brightness: 10 });
    });

    test('non-boolean power/holding default to false', () => {
        assert.deepEqual(sanitizeStateMessage({ power: 'on', holding: 1, brightness: 10 }),
            { power: false, holding: false, brightness: 10 });
    });

    test('missing/non-number brightness becomes null (caller should skip applying it)', () => {
        assert.equal(sanitizeStateMessage({ power: true, holding: false }).brightness, null);
        assert.equal(
            sanitizeStateMessage({ power: true, holding: false, brightness: 'bright' }).brightness,
            null);
    });

    test('NaN brightness becomes null', () => {
        assert.equal(sanitizeStateMessage({ brightness: NaN }).brightness, null);
    });
});

describe('logoBrightnessFilter', () => {
    test('0 stays at 0', () => {
        assert.equal(logoBrightnessFilter(0), 0);
    });

    test('max stays at 1', () => {
        assert.equal(logoBrightnessFilter(255), 1);
    });

    test('low slider values still render a fairly bright logo', () => {
        // value 25 is ~10% of the slider; the steep curve keeps the logo
        // well above the linear 0.1 it would otherwise sit at.
        assert.ok(logoBrightnessFilter(25) > 0.4);
    });

    test('is steeper than linear across the range', () => {
        const mid = 128;
        assert.ok(logoBrightnessFilter(mid) > mid / 255);
    });
});
