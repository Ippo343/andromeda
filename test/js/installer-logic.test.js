const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const { boardLabel, formatPartsTable } = require('../../web-installer/js/installer-logic.js');

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
        assert.deepEqual(formatPartsTable({ boards: 'nope' }), []);
    });

    // The real shape assemble_site.py's _write_version_info() writes: three
    // boards, each already offset-sorted, bootloader at a chip-specific
    // address (0x1000 on ESP32, 0x0 on S3/C3 - see dump_flash_parts.py).
    const realVersionJson = {
        tag: 'v0.9-deep-space-network',
        releaseUrl: 'https://github.com/Ippo343/andromeda/releases/tag/v0.9-deep-space-network',
        boards: [
            {
                board: 'esp32_wroom',
                chipFamily: 'ESP32',
                label: 'ESP32-WROOM',
                parts: [
                    { name: 'bootloader', offsetHex: '0x1000' },
                    { name: 'partitions', offsetHex: '0x8000' },
                    { name: 'boot_app0', offsetHex: '0xe000' },
                    { name: 'firmware', offsetHex: '0x10000' },
                    { name: 'littlefs', offsetHex: '0x350000' },
                ],
            },
            {
                board: 'esp32_s3_zero',
                chipFamily: 'ESP32-S3',
                label: 'ESP32-S3-Zero',
                parts: [
                    { name: 'bootloader', offsetHex: '0x0' },
                    { name: 'partitions', offsetHex: '0x8000' },
                    { name: 'boot_app0', offsetHex: '0xe000' },
                    { name: 'firmware', offsetHex: '0x10000' },
                    { name: 'littlefs', offsetHex: '0x350000' },
                ],
            },
            {
                board: 'esp32_c3_zero',
                chipFamily: 'ESP32-C3',
                label: 'ESP32-C3-Zero',
                parts: [
                    { name: 'bootloader', offsetHex: '0x0' },
                    { name: 'partitions', offsetHex: '0x8000' },
                    { name: 'boot_app0', offsetHex: '0xe000' },
                    { name: 'firmware', offsetHex: '0x10000' },
                    { name: 'littlefs', offsetHex: '0x350000' },
                ],
            },
        ],
    };

    test('renders one row per board from a real version.json fixture', () => {
        const rows = formatPartsTable(realVersionJson);
        assert.equal(rows.length, 3);
        assert.deepEqual(rows.map((r) => r.board), [
            'esp32_wroom',
            'esp32_s3_zero',
            'esp32_c3_zero',
        ]);
        assert.deepEqual(rows.map((r) => r.label), [
            'ESP32-WROOM',
            'ESP32-S3-Zero',
            'ESP32-C3-Zero',
        ]);
        // ESP32 keeps its 0x1000 bootloader first; S3 starts at 0x0.
        assert.equal(rows[0].parts[0], 'bootloader @ 0x1000');
        assert.equal(rows[1].parts[0], 'bootloader @ 0x0');
        assert.ok(rows.every((r) => r.parts.length === 5));
    });

    test('falls back to boardLabel(chipFamily) when a board has no label', () => {
        const rows = formatPartsTable({
            boards: [{ board: 'esp32_c3_zero', chipFamily: 'ESP32-C3', parts: [] }],
        });
        assert.equal(rows[0].label, 'ESP32-C3-Zero');
    });
});
