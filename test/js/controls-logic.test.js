const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    pickInitialTab,
    findEffectIdByName,
    holdIcon,
    powerButtonState,
    rgbToHistogramWidths,
    hsvToRgb,
    logoBrightnessFilter,
    parseLogLevel,
    filterLogText,
    formatUptime,
    formatHeap,
    rssiLabel,
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

describe('parseLogLevel', () => {
    test('reads the severity token from a real log line', () => {
        assert.equal(parseLogLevel('12345 | WRN | WiFi connecting'), 3);
        assert.equal(parseLogLevel('12345 | ERR | boom'), 2);
        assert.equal(parseLogLevel('12345 | VRB | chatty'), 6);
    });

    test('returns 0 for lines with no token', () => {
        assert.equal(parseLogLevel('   ...continuation'), 0);
        assert.equal(parseLogLevel(''), 0);
        assert.equal(parseLogLevel(undefined), 0);
    });
});

describe('filterLogText', () => {
    const log = [
        '1 | INF | started',
        '2 | WRN | flaky',
        '3 | ERR | dropped',
        'bare continuation line',
        '4 | VRB | noise',
    ].join('\n');

    test('minRank >= 6 (or falsy) returns everything', () => {
        assert.equal(filterLogText(log, 6), log);
        assert.equal(filterLogText(log, 0), log);
    });

    test('keeps only lines at/above the requested severity, plus token-less lines', () => {
        const out = filterLogText(log, 3).split('\n');
        assert.deepEqual(out, ['2 | WRN | flaky', '3 | ERR | dropped', 'bare continuation line']);
    });

    test('empty input stays empty', () => {
        assert.equal(filterLogText('', 2), '');
    });

    test('preserves a trailing newline', () => {
        assert.equal(filterLogText('1 | ERR | x\n', 2), '1 | ERR | x\n');
    });
});

describe('formatUptime', () => {
    test('sub-day is HH:MM:SS', () => {
        assert.equal(formatUptime((3 * 3600 + 4 * 60 + 5) * 1000), '03:04:05');
    });

    test('multi-day prefixes the day count', () => {
        assert.equal(formatUptime((2 * 86400 + 60) * 1000), '2d 00:01:00');
    });

    test('non-finite / negative is "--"', () => {
        assert.equal(formatUptime(NaN), '--');
        assert.equal(formatUptime(-1), '--');
    });
});

describe('formatHeap', () => {
    test('scales bytes / KB / MB', () => {
        assert.equal(formatHeap(512), '512 B');
        assert.equal(formatHeap(145735), '142.3 KB');
        assert.equal(formatHeap(3 * 1024 * 1024), '3.00 MB');
    });

    test('non-finite is "--"', () => {
        assert.equal(formatHeap(undefined), '--');
    });
});

describe('rssiLabel', () => {
    test('buckets by signal strength', () => {
        assert.equal(rssiLabel(-50).quality, 'excellent');
        assert.equal(rssiLabel(-60).quality, 'good');
        assert.equal(rssiLabel(-75).quality, 'fair');
        assert.equal(rssiLabel(-90).quality, 'weak');
    });

    test('includes a dBm display string', () => {
        assert.equal(rssiLabel(-60).text, '-60 dBm');
    });

    test('0 or non-finite means no station link', () => {
        assert.deepEqual(rssiLabel(0), { quality: 'unknown', text: '--' });
        assert.deepEqual(rssiLabel(NaN), { quality: 'unknown', text: '--' });
    });
});
