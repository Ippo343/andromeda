// DOM-free logic for the "set model" step (#105/#187) - the lab-preflash
// mechanism that lets the web installer set a freshly-flashed board's model
// over the same USB-serial connection used for flashing, rather than a
// separate WiFi/web-UI round trip. Pulled out the same way installer-logic.js
// is - see test/js/model-select-logic.test.js. model-select.js does the
// actual DOM/Web Serial wiring and stays untested, mirroring installer.js.
//
// Sends the exact same commands the on-device web UI already sends over its
// WebSocket (see include/ws-command-parser.h's "model"/"reboot" branches) -
// this is a new transport for an existing, already-safe command, not a new
// on-device capability.

// MODELS (web-installer/js/models.js) -> [{value, label}] for a <select>.
function buildModelOptions(models) {
    if (!Array.isArray(models)) return [];
    return models.map((m) => ({ value: String(m.id), label: m.label }));
}

// The exact newline-terminated JSON line WsCommandParser::parse() expects
// for a MODEL command (see include/ws-command-parser.h).
function buildModelCommand(modelId) {
    return `{"type":"model","id":${modelId}}\n`;
}

// Mirrors the REBOOT command comms.cpp's WS handler already accepts - sent
// after the MODEL command, since CommandType::MODEL does not itself reboot
// (see mission-control.cpp).
function buildRebootCommand() {
    return '{"type":"reboot"}\n';
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { buildModelOptions, buildModelCommand, buildRebootCommand };
}
