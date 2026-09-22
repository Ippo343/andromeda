// DOM-free logic for the optional WiFi provisioning step (#245) - lets the web installer
// push a freshly-flashed board's WiFi SSID/password over the same USB-serial connection used
// for flashing, so it can join WiFi without ever entering AP mode. Pulled out the same way
// model-select-logic.js is - see test/js/wifi-provision-logic.test.js. wifi-provision.js does
// the actual DOM/Web Serial wiring and stays untested, mirroring model-select.js/installer.js.
//
// Serial-only, not the WebSocket the on-device setup page's WiFi form uses (see
// include/serial-command-dispatch.h's "wifi_credentials" case) - the device may have no WiFi
// at all yet at this point. Sent BEFORE the model-select step's "model"/"reboot" commands, on
// the same already-open serial port session - see model-select.js for where this plugs in and
// why the ordering matters.

// The exact newline-terminated JSON line SerialCommandDispatch::dispatch() expects for the
// wifi_credentials verb. `password` may be empty (open network) - the device treats a missing
// field the same way, but sending it explicitly keeps the wire format unambiguous.
function buildWifiCredentialsCommand(ssid, password) {
    return `${JSON.stringify({ type: 'wifi_credentials', ssid, password: password || '' })}\n`;
}

// Only a non-empty SSID is worth checking client-side - the device enforces the real 802.11
// length limits (WifiManager::MAX_SSID_LENGTH/MAX_PASSWORD_LENGTH) and reports a specific
// rejection reason back over the ack line if either is exceeded, so there's no need to
// duplicate those bounds here.
function isSsidValid(ssid) {
    return typeof ssid === 'string' && ssid.trim().length > 0;
}

// Shared by parseWifiResultLine/parseWifiCredentialsAck below: finds the first '{' and
// JSON.parses from there, tolerating the "<<< " reply-frame prefix device output carries and
// a bare JSON line (useful for the simulator/tests). Returns null for anything that isn't
// parseable JSON.
function parseDeviceJsonLine(line) {
    if (typeof line !== 'string') return null;
    const jsonStart = line.indexOf('{');
    if (jsonStart === -1) return null;
    try {
        return JSON.parse(line.slice(jsonStart));
    } catch (_) {
        return null;
    }
}

// Parses one line of device output looking for the unsolicited
// {"type":"wifi_result","ok":...,"reason":"..."} line the device emits once the async
// connect-and-persist probe resolves (~10s after the immediate ack) - see comms.cpp's
// saveWorkerTask / Comms::startCredentialTest. Returns null for a line that isn't one -
// including the immediate {"type":"ack","cmd":"wifi_credentials",...} ack for the verb
// itself (see parseWifiCredentialsAck below for that one), or a plain ArduinoLog line.
function parseWifiResultLine(line) {
    const parsed = parseDeviceJsonLine(line);
    if (!parsed || parsed.type !== 'wifi_result') return null;
    return { ok: !!parsed.ok, reason: typeof parsed.reason === 'string' ? parsed.reason : '' };
}

// Parses the immediate {"type":"ack","cmd":"wifi_credentials","ok":...,"error":"..."} line
// SerialCommandDispatch::dispatch() sends back for the verb itself - distinct from
// parseWifiResultLine's later, unsolicited outcome. ok:false here (e.g. an oversized SSID, or
// a probe already in flight) means no job was ever queued, so the caller must not then wait
// around for a wifi_result line that will never arrive.
function parseWifiCredentialsAck(line) {
    const parsed = parseDeviceJsonLine(line);
    if (!parsed || parsed.type !== 'ack' || parsed.cmd !== 'wifi_credentials') return null;
    return { ok: !!parsed.ok, error: typeof parsed.error === 'string' ? parsed.error : '' };
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        buildWifiCredentialsCommand,
        isSsidValid,
        parseWifiResultLine,
        parseWifiCredentialsAck,
    };
}
