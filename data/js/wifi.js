// WiFi setup page functionality

let selectedNetwork = null;

function scanNetworks() {
    const status = document.getElementById('status');
    const networks = document.getElementById('networks');
    const scanBtn = document.getElementById('scanBtn');

    scanBtn.disabled = true;
    scanBtn.innerHTML = '<span class="spinner"></span>Scanning...';

    status.innerHTML = '<div class="status loading"><span class="spinner"></span>Scanning for WiFi networks...</div>';
    networks.innerHTML = '';
    selectedNetwork = null;

    let pollAttempts = 0;
    const maxPollAttempts = 20;

    function pollScan() {
        // Without a timeout, a request that never gets a response (the device gone dark
        // mid-scan) hangs forever - neither resolving nor rejecting - leaving the spinner
        // stuck and the button disabled with no way out short of a page reload, since the
        // pollAttempts budget below only ever kicks in for a response that did arrive.
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 5000);

        fetch('/scan', { signal: controller.signal })
            .finally(() => clearTimeout(timeout))
            .then(response => {
                if (!response.ok) {
                    throw new Error('Scan request failed');
                }
                return response.json();
            })
            .then(data => {
                if (data.networks && data.networks.length > 0) {
                    displayNetworks(data.networks);
                    return;
                }

                if (data.error) {
                    status.innerHTML = '<div class="status error">Scan failed: ' + escapeHtml(data.error) + '</div>';
                    scanBtn.disabled = false;
                    scanBtn.innerHTML = 'Scan for Networks';
                    appendManualEntryOption(networks);
                    return;
                }

                if (data.status === "scanning") {
                    pollAttempts++;
                    if (pollAttempts < maxPollAttempts) {
                        setTimeout(pollScan, 1000);
                    } else {
                        status.innerHTML = '<div class="status error">Scan timeout. Please try again.</div>';
                        scanBtn.disabled = false;
                        scanBtn.innerHTML = 'Scan for Networks';
                        appendManualEntryOption(networks);
                    }
                    return;
                }

                if (data.networks && data.networks.length === 0 && !data.status) {
                    networks.innerHTML = '<div class="status error">No networks found. Make sure WiFi networks are available and try scanning again.</div>';
                    status.innerHTML = '';
                    scanBtn.disabled = false;
                    scanBtn.innerHTML = 'Scan for Networks';
                    appendManualEntryOption(networks);
                    return;
                }

                // A response shaped like none of the above (no networks/error/scanning
                // status - a malformed/empty body, or an edge case in the server's response
                // building) previously left the spinner running and the button disabled with
                // no way out short of a page reload.
                status.innerHTML = '<div class="status error">Unexpected response from device. Please try again.</div>';
                scanBtn.disabled = false;
                scanBtn.innerHTML = 'Scan for Networks';
                appendManualEntryOption(networks);
            })
            .catch(error => {
                console.error('Scan error:', error);
                status.innerHTML = '<div class="status error">Scan failed: ' + escapeHtml(error.message) + '</div>';
                scanBtn.disabled = false;
                scanBtn.innerHTML = 'Scan for Networks';
                appendManualEntryOption(networks);
            });
    }

    function displayNetworks(networkList) {
        status.innerHTML = '';
        scanBtn.disabled = false;
        scanBtn.innerHTML = 'Scan for Networks';

        networks.innerHTML = '<strong>Available Networks (click to select):</strong>';

        networkList.sort((a, b) => b.rssi - a.rssi);

        networkList.forEach(network => {
            if (network.ssid && network.ssid.trim() !== '') {
                const div = document.createElement('div');
                div.className = 'network-option';
                div.onclick = () => selectNetwork(network, div);

                const signalBars = getSignalBars(network.rssi);
                const lockIcon = network.encryption !== 'none' ? '🔒' : '🔓';

                div.innerHTML = `
                    <div>
                        <div class="network-name">${escapeHtml(network.ssid)}</div>
                        <div class="network-info">${network.encryption}</div>
                    </div>
                    <div class="signal-strength">
                        ${signalBars} ${network.rssi}dBm ${lockIcon}
                    </div>
                `;
                networks.appendChild(div);
            }
        });

        appendManualEntryOption(networks);
    }

    pollScan();
}

// Scanning is the primary way to pick a network - the SSID text box stays
// hidden (see wifi-setup.html) until either a scan result or this entry is
// picked, so there's nothing for iOS to aggressively autofocus/zoom into
// while a scan is in progress. Appended after every scan outcome (results,
// no networks found, error, or timeout) so manual entry is never a dead end.
function appendManualEntryOption(container) {
    const div = document.createElement('div');
    div.className = 'network-option manual-entry';
    div.textContent = 'Enter manually…';
    div.onclick = () => enterManually(div);
    container.appendChild(div);
}

function revealCredentialsForm() {
    document.getElementById('wifiForm').classList.remove('hidden');
}

function selectNetwork(network, element) {
    document.querySelectorAll('.network-option').forEach(el => {
        el.classList.remove('selected');
    });

    element.classList.add('selected');
    selectedNetwork = network;

    document.getElementById('ssid').value = network.ssid;
    document.getElementById('password').value = '';

    revealCredentialsForm();

    if (network.encryption !== 'none') {
        document.getElementById('password').focus();
    }
}

// "Enter manually..." from the scan list: reveals the same form as
// selectNetwork() but with an empty, editable SSID field instead of one
// pre-filled from a scan result - covers hidden SSIDs and networks the
// scan missed. Focuses the SSID field directly, since this is the one path
// where the user actually wants to type into it right away.
function enterManually(element) {
    document.querySelectorAll('.network-option').forEach(el => {
        el.classList.remove('selected');
    });
    if (element) element.classList.add('selected');
    selectedNetwork = null;

    const ssidField = document.getElementById('ssid');
    ssidField.value = '';
    document.getElementById('password').value = '';

    revealCredentialsForm();
    ssidField.focus();
}

function getSignalBars(rssi) {
    if (rssi >= -50) return '▰▰▰▰';
    if (rssi >= -60) return '▰▰▰▱';
    if (rssi >= -70) return '▰▰▱▱';
    if (rssi >= -80) return '▰▱▱▱';
    return '▱▱▱▱';
}

function togglePassword() {
    const passwordField = document.getElementById('password');
    const checkbox = document.getElementById('showPassword');

    passwordField.type = checkbox.checked ? 'text' : 'password';
}

function setFormBusy(busy) {
    document.getElementById('wifiForm')
        .querySelectorAll('input, button')
        .forEach(el => { el.disabled = busy; });
}

function resetConnectButton() {
    setFormBusy(false);
    document.getElementById('connectBtn').innerHTML = 'Connect to WiFi';
}

// AJAX submit (returns false so the native form POST never fires). The device
// can't run the ~10s blocking connection probe on its web-server task without
// tripping its watchdog (#114), so /save kicks the probe to a worker and
// answers 202 immediately; we then poll /save-status for the verdict, mirroring
// the /scan -> pollScan() pattern above. This is what restores inline "bad
// password" feedback that the synchronous full-page POST couldn't carry.
function submitCredentials(event) {
    if (event) event.preventDefault();

    const ssid = document.getElementById('ssid').value.trim();
    const password = document.getElementById('password').value;

    if (!ssid) {
        showStatus('Please enter a WiFi network name', 'error');
        return false;
    }

    setFormBusy(true);
    document.getElementById('connectBtn').innerHTML = '<span class="spinner"></span>Connecting...';
    // ssid is attacker-controlled (a scanned SSID can be anything a nearby radio
    // broadcasts, e.g. "<img src=x onerror=...>") and every showStatus() call below
    // builds its message via innerHTML, so it must be escaped at every interpolation
    // site here, not just this first one.
    showStatus('Testing connection to "' + escapeHtml(ssid) + '"...', 'loading');

    fetch('/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ ssid: ssid, password: password }).toString()
    })
        .then(response => {
            if (response.status === 202) {
                pollSaveStatus(ssid);
                return;
            }
            // 400 (validation) / 500 - surface the server's own message.
            return response.text().then(text => {
                showStatus(escapeHtml(text) || ('Save failed (' + response.status + ')'), 'error');
                resetConnectButton();
            });
        })
        .catch(error => {
            showStatus('Save failed: ' + escapeHtml(error.message), 'error');
            resetConnectButton();
        });

    return false;
}

function pollSaveStatus(ssid) {
    let attempts = 0;
    const maxAttempts = 25;  // ~25s, comfortably past the device's 10s probe

    function poll() {
        // Same guard as pollScan(): a request that never gets a response (the device
        // juggling the radio, or briefly dropping the setup AP) must not hang the poll.
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 5000);

        fetch('/save-status', { signal: controller.signal })
            .finally(() => clearTimeout(timeout))
            .then(response => {
                if (!response.ok) throw new Error('status ' + response.status);
                return response.text();
            })
            .then(state => {
                const s = (state || '').trim();

                if (s === 'connected') {
                    // The addresses panel above (loadDeviceAddresses(), filled before this
                    // submit even happened) is the reliable place this was already shown -
                    // this message is a bonus for whichever platforms' setup connections
                    // survive long enough to read it (see issue #135).
                    showStatus('Connected to "' + escapeHtml(ssid) + '"! The device is restarting to join '
                        + 'that network - reconnect your phone or laptop to your normal WiFi, then use '
                        + 'one of the addresses above to reach it again.', 'success');
                    return;
                }

                if (s === 'failed') {
                    showStatus('Could not connect to "' + escapeHtml(ssid) + '". Double-check the password '
                        + 'and try again.', 'error');
                    resetConnectButton();
                    return;
                }

                // pending
                if (++attempts < maxAttempts) {
                    setTimeout(poll, 1000);
                } else {
                    showStatus('Still trying to reach "' + escapeHtml(ssid) + '". If the device dropped the '
                        + 'setup network, wait a moment for it to reappear and reload this page.',
                        'error');
                    resetConnectButton();
                }
            })
            .catch(() => {
                // One missed poll isn't fatal - the device may be mid-probe with the radio
                // busy. Keep trying within the same budget.
                if (++attempts < maxAttempts) {
                    setTimeout(poll, 1000);
                } else {
                    showStatus('Lost contact with the device while connecting. Reconnect to the '
                        + 'setup network to check, or try again.', 'error');
                    resetConnectButton();
                }
            });
    }

    poll();
}

function resetCredentials() {
    if (confirm('This will clear all saved WiFi settings and restart the device. Continue?')) {
        showStatus('Clearing WiFi settings...', 'loading');

        fetch('/reset', { method: 'POST' })
            .then(response => {
                if (response.ok) {
                    showStatus('Settings cleared! Device is restarting...', 'success');
                } else {
                    throw new Error('Reset failed');
                }
            })
            .catch(error => {
                showStatus('Reset failed: ' + escapeHtml(error.message), 'error');
            });
    }
}

// Fills #deviceAddresses from GET /device-info, and leaves it there for the rest of the
// page's life - see wifi-setup.html's comment on why this can't wait until /save-status
// resolves. formatDeviceAddresses (wifi-logic.js) already tolerates a missing/malformed
// payload; a network failure here just leaves the panel hidden rather than blocking setup.
function loadDeviceAddresses() {
    const panel = document.getElementById('deviceAddresses');
    if (!panel) return;

    fetch('/device-info')
        .then(response => (response.ok ? response.json() : null))
        .then(info => {
            const addresses = formatDeviceAddresses(info);
            if (addresses.length === 0) return;

            const items = addresses
                .map((addr, i) => `
                    <li>
                        <code>${escapeHtml(addr)}</code>
                        <button type="button" class="copy-btn" data-addr="${escapeHtml(addr)}">Copy</button>
                    </li>
                `)
                .join('');

            panel.innerHTML = `
                <p>Once connected, this device will be reachable at:</p>
                <ul>${items}</ul>
            `;
            panel.classList.remove('hidden');

            panel.querySelectorAll('.copy-btn').forEach(btn => {
                btn.addEventListener('click', () => {
                    navigator.clipboard.writeText(btn.dataset.addr).then(() => {
                        const original = btn.textContent;
                        btn.textContent = 'Copied!';
                        setTimeout(() => { btn.textContent = original; }, 1500);
                    }).catch(() => {});
                });
            });
        })
        .catch(() => {
            // No addresses shown is a worse UX than none, but not a broken page - the
            // reset-and-try-again path (or the eventual success message) still works.
        });
}

function showStatus(message, type) {
    const status = document.getElementById('status');
    const spinner = type === 'loading' ? '<span class="spinner"></span>' : '';
    status.innerHTML = `<div class="status ${type}">${spinner}${message}</div>`;
}

// Initialize page
function initWifiPage() {
    const h1 = document.getElementById("logo");
    h1.style.setProperty('--grad', randomGradient());

    setTimeout(scanNetworks, 500);
    loadDeviceAddresses();
}

// If the page is restored from the bfcache (user navigated away mid-submit and
// came back), drop any leftover busy state so the form is usable again.
window.addEventListener('pageshow', function(event) {
    if (event.persisted) resetConnectButton();
});

document.addEventListener('DOMContentLoaded', initWifiPage);