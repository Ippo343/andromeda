// DOM-free logic for the browser web installer (#162), pulled out into its
// own module the same way data/js/controls-logic.js is - see
// test/js/installer-logic.test.js. installer.js does the actual DOM wiring
// and stays untested, mirroring controls.js/comms.cpp.
//
// Everything exported here is called by installer.js (#192 - a shipped,
// unit-tested module that the page never executed is worse than no module):
// formatPartsTable() renders the per-board flash layout from version.json,
// and boardLabel() names each board. Browser-support messaging is left to
// <esp-web-install-button>'s own unsupported/not-allowed slots in
// index.html - the single source of truth, not duplicated here.

const BOARD_LABELS = {
    ESP32: 'ESP32-WROOM',
    'ESP32-S3': 'ESP32-S3-Zero',
    'ESP32-C3': 'ESP32-C3-Zero',
};

function boardLabel(chipFamily) {
    return BOARD_LABELS[chipFamily] || chipFamily || 'Unknown board';
}

// version.json (build-scripts/assemble_site.py's _write_version_info) ships a
// boards[] array with each board's chipFamily, label and flash parts. Turn it
// into rows ready for the page: one per board, parts sorted low offset first
// (bootloader -> ... -> filesystem), each formatted "<name> @ <offsetHex>".
function formatPartsTable(versionInfo) {
    if (!versionInfo || !Array.isArray(versionInfo.boards)) return [];
    return versionInfo.boards.map((board) => ({
        board: board.board,
        label: board.label || boardLabel(board.chipFamily),
        parts: [...(board.parts || [])]
            .sort((a, b) => parseInt(a.offsetHex, 16) - parseInt(b.offsetHex, 16))
            .map((p) => `${p.name} @ ${p.offsetHex}`),
    }));
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { boardLabel, formatPartsTable };
}
