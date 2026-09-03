const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    describeBrowserSupport,
    boardLabel,
    formatPartsTable,
    releaseUrl,
} = require('../../web-installer/js/installer-logic.js');

describe('describeBrowserSupport', () => {
    test('ok when secure context and Web Serial are both present', () => {
        const result = describeBrowserSupport({ serialSupported: true, secureContext: true });
        assert.equal(result.ok, true);
    });

    test('flags a missing secure context before Web Serial support', () => {
        const result = describeBrowserSupport({ serialSupported: true, secureContext: false });
        assert.equal(result.ok, false);
        assert.match(result.message, /HTTPS/);
    });

    test('flags missing Web Serial support', () => {
        const result = describeBrowserSupport({ serialSupported: false, secureContext: true });
        assert.equal(result.ok, false);
        assert.match(result.message, /Chrome/);
    });
});

describe('boardLabel', () => {
    test('maps all three chip families', () => {
        assert.equal(boardLabel('ESP32'), 'ESP32-WROOM');
        assert.equal(boardLabel('ESP32-S3'), 'ESP32-S3-Zero');
        assert.equal(boardLabel('ESP32-C3'), 'ESP32-C3-Zero');
    });

    test('falls back to the raw value for an unknown chip family', () => {
        assert.equal(boardLabel('ESP32-P4'), 'ESP32-P4');
    });

    test('falls back to a safe label when given nothing', () => {
        assert.equal(boardLabel(undefined), 'Unknown board');
    });
});

describe('formatPartsTable', () => {
    const versionInfo = {
        boards: [
            {
                board: 'esp32_s3_zero',
                chipFamily: 'ESP32-S3',
                label: 'ESP32-S3-Zero',
                parts: [
                    { name: 'firmware', offsetHex: '0x10000' },
                    { name: 'bootloader', offsetHex: '0x0' },
                    { name: 'littlefs', offsetHex: '0x350000' },
                    { name: 'partitions', offsetHex: '0x8000' },
                    { name: 'boot_app0', offsetHex: '0xe000' },
                ],
            },
        ],
    };

    test('lists every part in ascending offset order', () => {
        const rows = formatPartsTable(versionInfo);
        assert.equal(rows.length, 1);
        assert.deepEqual(rows[0].parts, [
            'bootloader @ 0x0',
            'partitions @ 0x8000',
            'boot_app0 @ 0xe000',
            'firmware @ 0x10000',
            'littlefs @ 0x350000',
        ]);
    });

    test('returns an empty list for missing/malformed input', () => {
        assert.deepEqual(formatPartsTable(null), []);
        assert.deepEqual(formatPartsTable({}), []);
    });
});

describe('releaseUrl', () => {
    test('builds the GitHub release URL', () => {
        assert.equal(
            releaseUrl('Ippo343/andromeda', 'v0.9.1'),
            'https://github.com/Ippo343/andromeda/releases/tag/v0.9.1'
        );
    });

    test('returns empty string when repo or tag is missing', () => {
        assert.equal(releaseUrl('', 'v0.9.1'), '');
        assert.equal(releaseUrl('Ippo343/andromeda', ''), '');
    });
});
