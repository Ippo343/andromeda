const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    formatUptime,
    formatBytes,
    resetReasonLabel,
    formatTemp,
    formatCurrentDraw,
    formatBrightnessCeiling,
    metricTiles,
    firmwareLabel,
    isNewer,
    otaBadgeText,
    otaProgressLabel,
    otaStartRejectionLabel,
    isOtaTerminalState,
    isOtaPollDone,
    subscribeMetricsMessage,
    unsubscribeMetricsMessage,
    mergeMetrics,
    shouldPollMetricsHttp,
    WS_METRICS_GRACE_MS,
} = require('../../data/js/advanced-logic.js');

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

describe('formatCurrentDraw', () => {
    test('renders "<draw> / <budget> mA"', () => {
        assert.equal(formatCurrentDraw(234, 9000), '234 / 9000 mA');
    });
    test('rounds fractional values', () => {
        assert.equal(formatCurrentDraw(233.6, 9000), '234 / 9000 mA');
    });
    test('either value non-finite -> "-"', () => {
        assert.equal(formatCurrentDraw(null, 9000), '-');
        assert.equal(formatCurrentDraw(234, null), '-');
        assert.equal(formatCurrentDraw(NaN, NaN), '-');
    });
});

describe('formatBrightnessCeiling', () => {
    test('renders "<ceiling> / 255"', () => {
        assert.equal(formatBrightnessCeiling(152), '152 / 255');
    });
    test('rounds fractional values', () => {
        assert.equal(formatBrightnessCeiling(151.6), '152 / 255');
    });
    test('non-finite -> "-"', () => {
        assert.equal(formatBrightnessCeiling(null), '-');
        assert.equal(formatBrightnessCeiling(NaN), '-');
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
        currentMa: 234,
        maxMilliamps: 9000,
        brightnessCeiling: 152,
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
            'FPS', 'WiFi RSSI', 'CPU', 'Current draw', 'Brightness cap', 'Chip', 'Last reset',
        ]);
        assert.equal(tiles[0].value, '00h 01m 05s');
        assert.equal(tiles[1].value, '120.0 KB');
        assert.equal(tiles[4].value, '44.1 °C (approx)');
        assert.equal(tiles[5].value, '62');
        assert.equal(tiles[6].value, '-58 dBm');
        assert.equal(tiles[8].value, '234 / 9000 mA');
        assert.equal(tiles[9].value, '152 / 255');
        assert.equal(tiles[10].value, 'ESP32-S3');
        assert.equal(tiles[11].value, 'power-on');
    });

    test('null fps / rssi / cpu / current draw / brightness cap render as "-"', () => {
        const tiles = metricTiles({
            ...sample, fps: null, rssi: null, cpuMhz: null, currentMa: null, maxMilliamps: null,
            brightnessCeiling: null,
        });
        assert.equal(tiles[5].value, '-');
        assert.equal(tiles[6].value, '-');
        assert.equal(tiles[7].value, '-');
        assert.equal(tiles[8].value, '-');
        assert.equal(tiles[9].value, '-');
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

describe('otaStartRejectionLabel', () => {
    test('good weather: uses the server reason text', () => {
        assert.equal(
            otaStartRejectionLabel(409, 'an OTA task is already running'),
            "Couldn't start: an OTA task is already running",
        );
    });
    test('bad weather: falls back to the status code when the body is empty', () => {
        assert.equal(otaStartRejectionLabel(503, ''), "Couldn't start: HTTP 503");
        assert.equal(otaStartRejectionLabel(503, '   '), "Couldn't start: HTTP 503");
        assert.equal(otaStartRejectionLabel(0, null), "Couldn't start: HTTP 0");
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

describe('subscribeMetricsMessage / unsubscribeMetricsMessage', () => {
    test('emit the exact JSON WsCommandParser::parseSubscription expects', () => {
        assert.equal(subscribeMetricsMessage(), '{"type":"subscribe","topic":"metrics"}');
        assert.equal(unsubscribeMetricsMessage(), '{"type":"unsubscribe","topic":"metrics"}');
    });
});

describe('mergeMetrics', () => {
    test('a volatile-only frame does not blank fields from an earlier full frame', () => {
        const full = { chip: 'ESP32-S3', version: 'v1.0', uptimeMs: 100, fps: 60 };
        const volatile = { uptimeMs: 200, fps: 61 };
        const merged = mergeMetrics(full, volatile);
        assert.equal(merged.chip, 'ESP32-S3');
        assert.equal(merged.version, 'v1.0');
        assert.equal(merged.uptimeMs, 200);
        assert.equal(merged.fps, 61);
    });

    test('a later frame overrides matching keys from an earlier one', () => {
        const merged = mergeMetrics({ rssi: -70 }, { rssi: -50 });
        assert.equal(merged.rssi, -50);
    });

    test('"type" never leaks into the merged object', () => {
        const merged = mergeMetrics({}, { type: 'metrics', uptimeMs: 5 });
        assert.equal('type' in merged, false);
        assert.equal(merged.uptimeMs, 5);
    });

    test('a null or non-object frame is a no-op', () => {
        const prev = { chip: 'ESP32-S3' };
        assert.deepEqual(mergeMetrics(prev, null), prev);
        assert.deepEqual(mergeMetrics(prev, undefined), prev);
        assert.deepEqual(mergeMetrics(prev, 'not an object'), prev);
    });

    test('missing prev starts from an empty object', () => {
        assert.deepEqual(mergeMetrics(undefined, { fps: 42 }), { fps: 42 });
    });
});

describe('shouldPollMetricsHttp', () => {
    test('closed socket -> always poll', () => {
        assert.equal(shouldPollMetricsHttp(false, Date.now(), Date.now()), true);
        assert.equal(shouldPollMetricsHttp(false, null, Date.now()), true);
    });
    test('open socket, a frame arrived within the grace window -> no poll', () => {
        assert.equal(shouldPollMetricsHttp(true, 1000, 1000 + WS_METRICS_GRACE_MS - 1), false);
    });
    test('open socket, last frame is stale -> poll', () => {
        assert.equal(shouldPollMetricsHttp(true, 1000, 1000 + WS_METRICS_GRACE_MS), true);
    });
    test('open socket, no frame ever received -> poll', () => {
        assert.equal(shouldPollMetricsHttp(true, null, Date.now()), true);
    });
});
