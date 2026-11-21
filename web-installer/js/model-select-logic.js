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

// The console answers a MODEL command with its generic ack frame:
//   <<< {"type":"ack","cmd":"model","ok":true}
// Waiting for the "Factory config: Set model ID" log line instead is wrong:
// setModelId() returns before logging when the stored id already matches
// (#152), so re-flashing a board with its current model never acknowledged and
// the installer reported failure (#OT-4).
function parseModelAck(line) {
    if (typeof line !== 'string') return false;
    // Parse from the first '{' rather than requiring a clean "<<" prefix: the
    // console writer drops bytes when the host is not draining, and a log line
    // whose newline was lost glues itself in front of the next ack frame. The
    // sibling parseDeviceJsonLine does the same (#OT-4).
    const start = line.indexOf('{');
    if (start < 0) return false;
    try {
        const message = JSON.parse(line.slice(start).trim());
        return Boolean(message) && message.type === 'ack' && message.cmd === 'model'
            && message.ok === true;
    } catch (e) {
        return false;
    }
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { buildModelOptions, buildModelCommand, buildRebootCommand, parseModelAck };
}
