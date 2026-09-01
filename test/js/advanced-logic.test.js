const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    parseLogLine,
    levelClass,
    formatUptime,
    formatBytes,
    resetReasonLabel,
    formatTemp,
    metricTiles,
    firmwareLabel,
    isNewer,
    otaBadgeText,
    otaProgressLabel,
    isOtaTerminalState,
    isOtaPollDone,
} = require('../../data/js/advanced-logic.js');

describe('parseLogLine', () => {
    test('splits a well-formed line into ts / level / message', () => {
        const r = parseLogLine('123456 | INF | WiFi connected, IP 192.168.1.42');
        assert.deepEqual(r, {
            ts: 123456,
            level: 'INF',
            levelClass: 'log-inf',
            message: 'WiFi connected, IP 192.168.1.42',
        });
    });

    test('maps every known level tag to its class', () => {
        const cls = (tag) => parseLogLine(`1 | ${tag} | x`).levelClass;
        assert.equal(cls('FTL'), 'log-ftl');
        assert.equal(cls('ERR'), 'log-err');
        assert.equal(cls('WRN'), 'log-wrn');
        assert.equal(cls('INF'), 'log-inf');
        assert.equal(cls('TRC'), 'log-trc');
        assert.equal(cls('VRB'), 'log-vrb');
        assert.equal(cls('UNK'), 'log-raw');
    });

    test('keeps a pipe in the message intact', () => {
        assert.equal(parseLogLine('7 | WRN | a | b | c').message, 'a | b | c');
    });

    test('falls back to { raw } for a blank or non-matching line', () => {
        assert.deepEqual(parseLogLine(''), { raw: '', levelClass: 'log-raw' });
        assert.deepEqual(
            parseLogLine('    ...continuation without a prefix'),
            { raw: '    ...continuation without a prefix', levelClass: 'log-raw' });
    });
});

describe('levelClass', () => {
    test('is case-insensitive', () => {
        assert.equal(levelClass('err'), 'log-err');
    });
    test('unknown / empty -> log-raw', () => {
        assert.equal(levelClass('zzz'), 'log-raw');
        assert.equal(levelClass(''), 'log-raw');
        assert.equal(levelClass(undefined), 'log-raw');
    });
});

describe('formatUptime', () => {
    test('sub-hour uptime has no day part', () => {
        assert.equal(formatUptime((12 * 60 + 5) * 1000 + 900), '00h 12m 05s');
    });
    test('multi-day uptime includes the day count', () => {
        const ms = ((1 * 24 + 3) * 3600 + 12 * 60 + 5) * 1000;
        assert.equal(formatUptime(ms), '1d 03h 12m 05s');
    });
    test('non-finite / negative -> "-"', () => {
        assert.equal(formatUptime(NaN), '-');
        assert.equal(formatUptime(-1), '-');
    });
});

describe('formatBytes', () => {
    test('bytes below 1 KiB stay in B', () => {
        assert.equal(formatBytes(512), '512 B');
    });
    test('KiB and MiB ranges', () => {
        assert.equal(formatBytes(12 * 1024 + 307), '12.3 KB');
        assert.equal(formatBytes(1.4 * 1024 * 1024), '1.4 MB');
    });
    test('non-finite -> "-"', () => {
        assert.equal(formatBytes(NaN), '-');
    });
});

describe('resetReasonLabel', () => {
    test('known codes', () => {
        assert.equal(resetReasonLabel(1), 'power-on');
        assert.equal(resetReasonLabel(4), 'panic');
        assert.equal(resetReasonLabel(9), 'brownout');
    });
    test('unknown code falls through to "reset #<n>"', () => {
        assert.equal(resetReasonLabel(42), 'reset #42');
    });
});

describe('formatTemp', () => {
    test('finite reading is rounded and flagged approximate', () => {
        assert.equal(formatTemp(43.27), '43.3 °C (approx)');
    });
    test('non-finite / null -> "-" (firmware sends null for a bad sensor read)', () => {
        assert.equal(formatTemp(NaN), '-');
        assert.equal(formatTemp(null), '-');
    });
});

describe('metricTiles', () => {
    const sample = {
        uptimeMs: 65000,
        heapFree: 120 * 1024,
        heapMin: 90 * 1024,
        heapTotal: 300 * 1024,
        tempC: 44.1,
        fps: 61.7,
        rssi: -58,
        cpuMhz: 240,
        chip: 'ESP32-S3',
        resetReason: 1,
    };

    test('missing payload -> empty list', () => {
        assert.deepEqual(metricTiles(null), []);
        assert.deepEqual(metricTiles(undefined), []);
    });

    test('renders labels and formatted values in order', () => {
        const tiles = metricTiles(sample);
        assert.deepEqual(tiles.map((t) => t.label), [
            'Uptime', 'Free heap', 'Min free heap', 'Heap total', 'Temperature',
            'FPS', 'WiFi RSSI', 'CPU', 'Chip', 'Last reset',
        ]);
        assert.equal(tiles[0].value, '00h 01m 05s');
        assert.equal(tiles[1].value, '120.0 KB');
        assert.equal(tiles[4].value, '44.1 °C (approx)');
        assert.equal(tiles[5].value, '62');
        assert.equal(tiles[6].value, '-58 dBm');
        assert.equal(tiles[8].value, 'ESP32-S3');
        assert.equal(tiles[9].value, 'power-on');
    });

    test('null fps / rssi / cpu render as "-"', () => {
        const tiles = metricTiles({ ...sample, fps: null, rssi: null, cpuMhz: null });
        assert.equal(tiles[5].value, '-');
        assert.equal(tiles[6].value, '-');
        assert.equal(tiles[7].value, '-');
    });
});

describe('firmwareLabel', () => {
    test('returns the version string when present', () => {
        assert.equal(
            firmwareLabel({ version: 'v0.8-clanking-replicator-15-ge661613 (main)' }),
            'v0.8-clanking-replicator-15-ge661613 (main)');
    });
    test('missing version / payload -> "-"', () => {
        assert.equal(firmwareLabel({}), '-');
        assert.equal(firmwareLabel({ version: '' }), '-');
        assert.equal(firmwareLabel(null), '-');
        assert.equal(firmwareLabel(undefined), '-');
    });
});

describe('isNewer', () => {
    test('true only when remote code strictly exceeds local', () => {
        assert.equal(isNewer(457, 458), true);
        assert.equal(isNewer(457, 457), false);
        assert.equal(isNewer(458, 457), false);
    });
    test('non-numeric / missing -> false', () => {
        assert.equal(isNewer(457, undefined), false);
        assert.equal(isNewer(undefined, 458), false);
        assert.equal(isNewer('x', 'y'), false);
    });
});

describe('otaBadgeText', () => {
    test('names the tag when an update is available', () => {
        assert.equal(otaBadgeText({ updateAvailable: true, latestTag: 'v0.9-nova' }),
            'v0.9-nova available');
    });
    test('falls back when the tag is blank', () => {
        assert.equal(otaBadgeText({ updateAvailable: true, latestTag: '' }), 'update available');
    });
    test('empty string when nothing is available or payload is missing', () => {
        assert.equal(otaBadgeText({ updateAvailable: false, latestTag: 'v0.9' }), '');
        assert.equal(otaBadgeText({}), '');
        assert.equal(otaBadgeText(null), '');
    });
});

describe('otaProgressLabel', () => {
    test('per-phase text with percentage', () => {
        assert.equal(otaProgressLabel({ state: 'downloading', progress: 45 }), 'Downloading… 45%');
        assert.equal(otaProgressLabel({ state: 'writing-fw', progress: 80 }),
            'Writing firmware… 80%');
        assert.equal(otaProgressLabel({ state: 'writing-fs', progress: 12 }),
            'Writing filesystem… 12%');
        assert.equal(otaProgressLabel({ state: 'checking' }), 'Checking for updates…');
        assert.equal(otaProgressLabel({ state: 'rebooting' }),
            'Rebooting into the new firmware…');
    });
    test('failed surfaces the error, with a fallback', () => {
        assert.equal(otaProgressLabel({ state: 'failed', error: 'MD5 mismatch' }),
            'Update failed: MD5 mismatch');
        assert.equal(otaProgressLabel({ state: 'failed' }), 'Update failed: unknown error');
    });
    test('nothing to show for idle / completed-check states', () => {
        assert.equal(otaProgressLabel({ state: 'idle' }), '');
        assert.equal(otaProgressLabel({ state: 'uptodate' }), '');
        assert.equal(otaProgressLabel({ state: 'available' }), '');
        assert.equal(otaProgressLabel(null), '');
    });
    test('missing progress defaults to 0%', () => {
        assert.equal(otaProgressLabel({ state: 'downloading' }), 'Downloading… 0%');
    });
});

describe('isOtaTerminalState', () => {
    test('stops on resting / reboot states', () => {
        for (const s of ['idle', 'uptodate', 'available', 'failed', 'rebooting']) {
            assert.equal(isOtaTerminalState(s), true, s);
        }
    });
    test('keeps polling while work is in flight', () => {
        for (const s of ['checking', 'downloading', 'writing-fw', 'writing-fs']) {
            assert.equal(isOtaTerminalState(s), false, s);
        }
    });
});

describe('isOtaPollDone', () => {
    test('check flow (duringUpdate=false) matches isOtaTerminalState', () => {
        for (const s of ['idle', 'uptodate', 'available', 'failed', 'rebooting',
            'checking', 'downloading', 'writing-fw', 'writing-fs']) {
            assert.equal(isOtaPollDone(s, false), isOtaTerminalState(s), s);
        }
    });
    test('update flow keeps polling through a stale pre-trigger state', () => {
        // The bug this guards: a first poll racing the worker task sees
        // 'available' / 'idle' and must NOT end the loop mid-update.
        assert.equal(isOtaPollDone('available', true), false);
        assert.equal(isOtaPollDone('idle', true), false);
        assert.equal(isOtaPollDone('checking', true), false);
        assert.equal(isOtaPollDone('downloading', true), false);
        assert.equal(isOtaPollDone('writing-fw', true), false);
        assert.equal(isOtaPollDone('writing-fs', true), false);
        assert.equal(isOtaPollDone('rebooting', true), false);
    });
    test('update flow still ends on a real terminal result', () => {
        assert.equal(isOtaPollDone('uptodate', true), true);
        assert.equal(isOtaPollDone('failed', true), true);
    });
});
