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
                    status.innerHTML = '<div class="status error">Scan failed: ' + data.error + '</div>';
                    scanBtn.disabled = false;
                    scanBtn.innerHTML = 'Scan for Networks';
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
                    }
                    return;
                }

                if (data.networks && data.networks.length === 0 && !data.status) {
                    networks.innerHTML = '<div class="status error">No networks found. Make sure WiFi networks are available and try scanning again.</div>';
                    status.innerHTML = '';
                    scanBtn.disabled = false;
                    scanBtn.innerHTML = 'Scan for Networks';
                    return;
                }

                // A response shaped like none of the above (no networks/error/scanning
                // status - a malformed/empty body, or an edge case in the server's response
                // building) previously left the spinner running and the button disabled with
                // no way out short of a page reload.
                status.innerHTML = '<div class="status error">Unexpected response from device. Please try again.</div>';
                scanBtn.disabled = false;
                scanBtn.innerHTML = 'Scan for Networks';
            })
            .catch(error => {
                console.error('Scan error:', error);
                status.innerHTML = '<div class="status error">Scan failed: ' + error.message + '</div>';
                scanBtn.disabled = false;
                scanBtn.innerHTML = 'Scan for Networks';
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
    }

    pollScan();
}

function selectNetwork(network, element) {
    document.querySelectorAll('.network-option').forEach(el => {
        el.classList.remove('selected');
    });

    element.classList.add('selected');
    selectedNetwork = network;

    document.getElementById('ssid').value = network.ssid;
    document.getElementById('password').value = '';

    if (network.encryption !== 'none') {
        document.getElementById('password').focus();
    }
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

function validateForm() {
    const ssid = document.getElementById('ssid').value.trim();
    const connectBtn = document.getElementById('connectBtn');
    const form = document.getElementById('wifiForm');

    if (!ssid) {
        showStatus('Please enter a WiFi network name', 'error');
        return false;
    }

    connectBtn.disabled = true;
    connectBtn.innerHTML = '<span class="spinner"></span>Connecting...';

    // Lock the form down while the native POST to /save runs, but keep the
    // credential fields *submittable*: a `disabled` control is excluded from
    // the submitted form data (HTML spec), which dropped ssid/password from
    // the request and made the server reject it with "SSID required" (#114).
    // `readonly` inputs still submit; buttons/checkbox have no readonly, and
    // disabling those is harmless since they carry no submitted value.
    form.querySelectorAll('input, button').forEach(el => {
        if (el === connectBtn) return;
        if (el.tagName === 'INPUT' && (el.type === 'text' || el.type === 'password')) {
            el.readOnly = true;
        } else {
            el.disabled = true;
        }
    });

    showStatus('Saving "' + ssid + '" and restarting to connect...', 'loading');
    return true;
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
                showStatus('Reset failed: ' + error.message, 'error');
            });
    }
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
}

// Handle form submission errors (if user navigates back)
window.addEventListener('pageshow', function(event) {
    if (event.persisted) {
        document.getElementById('connectBtn').disabled = false;
        document.getElementById('connectBtn').innerHTML = 'Connect to WiFi';
        document.querySelectorAll('input, button').forEach(el => {
            el.disabled = false;
            el.readOnly = false;
        });
    }
});

document.addEventListener('DOMContentLoaded', initWifiPage);