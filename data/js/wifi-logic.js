// Pure, DOM-free helpers for the WiFi setup page's address panel (issue #135) - kept
// separate from wifi.js (DOM wiring) so this half is testable under Node, mirroring
// controls-logic.js's split from controls.js.

// Turns a raw /device-info response into the ordered list of full "http://name.local"
// addresses the setup page should display, with the device's current name always first.
// Tolerant of a missing/malformed payload (a fetch failure, or an older firmware that
// doesn't serve this route yet) - returns an empty array rather than throwing, so the
// caller can just skip rendering the panel instead of special-casing every field.
function formatDeviceAddresses(info) {
    if (!info || !Array.isArray(info.hosts)) return [];
    return info.hosts
        .filter(host => typeof host === 'string' && host.length > 0)
        .map(host => 'http://' + host);
}

// The SSID the device is already joined to, per /device-info's connectedSsid field (#210) -
// empty/absent while in AP mode or when talking to older firmware that doesn't serve the
// field yet. Lets the setup page show the current network directly instead of always
// kicking off a fresh scan on load.
function getConnectedSsid(info) {
    if (!info || typeof info.connectedSsid !== 'string') return null;
    const ssid = info.connectedSsid.trim();
    return ssid.length > 0 ? ssid : null;
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        formatDeviceAddresses,
        getConnectedSsid,
    };
}
