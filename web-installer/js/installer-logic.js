// DOM-free logic for the browser web installer (#162), pulled out into its
// own module the same way data/js/controls-logic.js is - see
// test/js/installer-logic.test.js. installer.js does the actual DOM wiring
// and stays untested, mirroring controls.js/comms.cpp.

function describeBrowserSupport({ serialSupported, secureContext }) {
    if (!secureContext) {
        return {
            ok: false,
            message: 'This page needs to be loaded over HTTPS for Web Serial to work.',
        };
    }
    if (!serialSupported) {
        return {
            ok: false,
            message:
                "This browser can't talk to USB serial devices. Use desktop Chrome, Edge, " +
                'or Opera - Web Serial isn’t available in Firefox, Safari, or on iOS/Android.',
        };
    }
    return { ok: true, message: '' };
}

const BOARD_LABELS = {
    ESP32: 'ESP32-WROOM',
    'ESP32-S3': 'ESP32-S3-Zero',
    'ESP32-C3': 'ESP32-C3-Zero',
};

function boardLabel(chipFamily) {
    return BOARD_LABELS[chipFamily] || chipFamily || 'Unknown board';
}

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

function releaseUrl(repo, tag) {
    if (!repo || !tag) return '';
    return `https://github.com/${repo}/releases/tag/${tag}`;
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { describeBrowserSupport, boardLabel, formatPartsTable, releaseUrl };
}
