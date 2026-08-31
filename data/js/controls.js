// Controls page functionality

// Navigate to WiFi setup (handles captive portal issues)
function goToWifiSetup() {
    window.location.href = '/wifi';
    setTimeout(() => {
        if (window.location.pathname !== '/wifi' && window.location.pathname !== '/setup') {
            window.open('/wifi', '_blank');
        }
    }, 500);
}

// Diagnostics / logs / device-model selector live on their own page now.
function goToAdvanced() {
    window.location.href = '/advanced.html';
}

// The device name field is on its own page - see device-name.js for why.
function goToDeviceName() {
    window.location.href = '/device-name.html';
}

// Update slider thumb color based on brightness value
function updateSliderThumb(slider) {
    const value = parseInt(slider.value);
    const percentage = (value / 255) * 100;

    // Calculate color from black to white
    const colorValue = Math.round((value / 255) * 255);
    const color = `rgb(${colorValue}, ${colorValue}, ${colorValue})`;

    slider.style.setProperty('--thumb-color', color);

    // Update logo brightness filter
    const logo = document.getElementById('logo');
    if (logo) {
        logo.style.setProperty('--logo-filt', logoBrightnessFilter(value));
    }
}

// Updates the color preview box, RGB label, and histogram bars together -
// shared by live color-wheel drags and incoming state broadcasts so they
// can't drift out of sync with each other.
function applyColorDisplay(r, g, b) {
    const colorPreview = document.getElementById('colorPreview');
    const colorInfo = document.getElementById('colorInfo');
    if (colorPreview) colorPreview.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
    if (colorInfo) {
        colorInfo.innerHTML =
            `<span style="color: #ff6b6b">${r}</span>, ` +
            `<span style="color: #6bff6b">${g}</span>, ` +
            `<span style="color: #6b6bff">${b}</span>`;
    }

    const widths = rgbToHistogramWidths(r, g, b);
    const histFillR = document.getElementById('histFillR');
    const histFillG = document.getElementById('histFillG');
    const histFillB = document.getElementById('histFillB');
    if (histFillR) histFillR.style.width = `${widths.r}%`;
    if (histFillG) histFillG.style.width = `${widths.g}%`;
    if (histFillB) histFillB.style.width = `${widths.b}%`;
}

// Color Wheel Class
class ColorWheel {
    constructor(canvasId, selectorId, previewId, infoId) {
        this.canvas = document.getElementById(canvasId);
        this.selector = document.getElementById(selectorId);
        this.preview = document.getElementById(previewId);
        this.info = document.getElementById(infoId);

        this.ctx = this.canvas.getContext('2d');
        this.radius = this.canvas.width / 2;
        this.centerX = this.radius;
        this.centerY = this.radius;

        this.isDragging = false;

        this.drawWheel();
        this.attachEvents();

        // Set initial position to center (white)
        this.updateColor(this.centerX, this.centerY);
    }

    drawWheel() {
        const imageData = this.ctx.createImageData(this.canvas.width, this.canvas.height);
        const data = imageData.data;

        for (let y = 0; y < this.canvas.height; y++) {
            for (let x = 0; x < this.canvas.width; x++) {
                const dx = x - this.centerX;
                const dy = y - this.centerY;
                const distance = Math.sqrt(dx * dx + dy * dy);

                if (distance <= this.radius) {
                    const angle = Math.atan2(dy, dx);
                    const hue = (angle * 180 / Math.PI + 360) % 360;
                    const saturation = distance / this.radius;

                    const rgb = hsvToRgb(hue, saturation, 1);

                    const index = (y * this.canvas.width + x) * 4;
                    data[index] = rgb[0];
                    data[index + 1] = rgb[1];
                    data[index + 2] = rgb[2];
                    data[index + 3] = 255;
                } else {
                    const index = (y * this.canvas.width + x) * 4;
                    data[index + 3] = 0;
                }
            }
        }

        this.ctx.putImageData(imageData, 0, 0);
    }

    attachEvents() {
        this.canvas.addEventListener('mousedown', (e) => this.startDrag(e));
        this.canvas.addEventListener('mousemove', (e) => this.drag(e));
        this.canvas.addEventListener('mouseup', () => this.stopDrag());
        this.canvas.addEventListener('mouseleave', () => this.stopDrag());

        this.canvas.addEventListener('touchstart', (e) => this.startDrag(e));
        this.canvas.addEventListener('touchmove', (e) => this.drag(e));
        this.canvas.addEventListener('touchend', () => this.stopDrag());
    }

    startDrag(e) {
        this.isDragging = true;
        this.handleInteraction(e);
    }

    drag(e) {
        if (this.isDragging) {
            this.handleInteraction(e);
        }
    }

    stopDrag() {
        this.isDragging = false;
    }

    handleInteraction(e) {
        e.preventDefault();
        // Measured live from the rendered box (CSS pixels) rather than
        // taken from this.centerX/this.radius (fixed from the canvas's
        // internal bitmap resolution at construction): if the rendered size
        // ever drifts from that bitmap size - DPI/zoom rounding, or the
        // mobile 70vw breakpoint - the two stop lining up, and the assumed
        // center sits off from the true visual center, which clamps the
        // drag short of the rim on one side and past it on the opposite side.
        const rect = this.canvas.getBoundingClientRect();
        const centerX = rect.width / 2;
        const centerY = rect.height / 2;
        const radius = Math.min(rect.width, rect.height) / 2;

        let clientX, clientY;
        if (e.touches) {
            clientX = e.touches[0].clientX;
            clientY = e.touches[0].clientY;
        } else {
            clientX = e.clientX;
            clientY = e.clientY;
        }

        let x = clientX - rect.left;
        let y = clientY - rect.top;

        const dx = x - centerX;
        const dy = y - centerY;
        const distance = Math.sqrt(dx * dx + dy * dy);

        if (distance > radius) {
            const scale = radius / distance;
            x = centerX + dx * scale;
            y = centerY + dy * scale;
        }

        this.updateColor(x, y, centerX, centerY, radius);
    }

    updateColor(x, y, centerX = this.centerX, centerY = this.centerY, radius = this.radius) {
        // Computed directly from (x, y) rather than sampled back off the
        // canvas: getImageData floors fractional coordinates toward -Infinity,
        // which in the NW quadrant (dx < 0, dy < 0) pushes the sampled pixel
        // further from center than intended, occasionally past the drawn
        // circle into the transparent border and reading back as (0,0,0).
        const dx = x - centerX;
        const dy = y - centerY;
        const distance = Math.sqrt(dx * dx + dy * dy);
        const angle = Math.atan2(dy, dx);
        const hue = (angle * 180 / Math.PI + 360) % 360;
        const saturation = Math.min(distance / radius, 1);

        const [r, g, b] = hsvToRgb(hue, saturation, 1);

        this.renderSelector(x, y, r, g, b);
        this.sendColor(r, g, b);
    }

    // Shared DOM-update tail for both a live drag (updateColor) and an
    // incoming state broadcast (setFromBroadcast) - moves the selector dot,
    // updates the preview/histogram via applyColorDisplay(), and re-tints the
    // logo shine. Never sends anything over the socket - that's sendColor()'s
    // job, called only from updateColor()'s own drag path.
    renderSelector(x, y, r, g, b) {
        this.selector.style.left = `${x}px`;
        this.selector.style.top = `${y}px`;

        const colorString = `rgb(${r}, ${g}, ${b})`;
        this.selector.style.backgroundColor = colorString;

        applyColorDisplay(r, g, b);

        const logo = document.getElementById('logo');
        const shineGradient = `linear-gradient(135deg,
            ${colorString} 40%,
            white 50%,
            ${colorString} 60%)`;

        logo.style.backgroundImage = shineGradient;
        logo.style.backgroundSize = '200% auto';
        logo.style.animation = 'shine-move 3s linear infinite';
        logo.style.webkitBackgroundClip = 'text';
        logo.style.webkitTextFillColor = 'transparent';
    }

    // Moves the selector to reflect an incoming state broadcast (a color
    // changed from another client, or pinned via the random rotation)
    // without re-sending it back over the socket - calling sendColor() here
    // would create a feedback loop with the broadcast that triggered this.
    setFromBroadcast(r, g, b) {
        const { h, s } = rgbToHsv(r, g, b);
        const { x, y } = hsToWheelPosition(h, s, this.centerX, this.centerY, this.radius);
        this.renderSelector(x, y, r, g, b);
    }

    sendColor(r, g, b) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({type: 'color', r, g, b}));
        }
    }
}

// WebSocket management
let ws = null;

// Reconnect attempt counter (jittered exponential backoff - see
// nextReconnectDelayMs()) and the previous attempt's timer, so a manual
// reconnect (e.g. a fresh connectWebSocket() call from elsewhere) can't
// stack a second pending retry on top of an existing one.
let reconnectAttempt = 0;
let reconnectTimer = null;

// Server-side auto-ping keeps the WS connection itself alive (see comms.cpp),
// but that only prevents the OS/router from reaping an idle connection - it
// doesn't tell the browser tab anything. Without a client-side check, a WiFi
// drop that doesn't produce a clean TCP FIN (the normal case for the device
// losing power or moving out of range) leaves readyState stuck at OPEN for
// minutes: no close/error event fires, the UI never dims, and every button
// press silently does nothing. Track the last time *any* message arrived and
// flag the connection stale if too long passes without one - the state
// broadcast's own 5s keepalive (see comms.cpp's STATE_KEEPALIVE_INTERVAL_MS)
// means one should always show up well inside this window on a live link.
let lastMessageAt = 0;
let staleCheckTimer = null;
const STALE_CONNECTION_MS = 15000;

function markConnectionStale() {
    document.body.classList.add('ws-disconnected');
    if (ws) {
        try { ws.close(); } catch (e) { /* already closing/closed */ }
    }
}

// Power button toggle state - module scope so both the click handler and
// incoming state broadcasts (handleServerMessage) can read/update it.
let deviceIsOn = true;

// Hold button toggle state - mirrors deviceIsOn's role but for HOLD/RESUME.
let deviceIsHolding = false;

// True while the brightness slider is actively being dragged, so an
// incoming state broadcast (e.g. triggered from another tab) doesn't yank
// the slider out from under the user mid-drag.
let sliderActivelyDragging = false;

// True until the first state message after a (re)connect has picked the
// initial mode tab (see pickInitialTab() in controls-logic.js) - after
// that, tab switching is 100% user-driven (clicking a tab or a shuffle
// button), never re-derived from later state broadcasts.
let awaitingInitialTab = true;

// Lazily constructed on the Color tab's first activation, exactly like the
// old color-picker drawer used to defer building the wheel until opened.
let colorWheel = null;

// A color broadcast that arrived before colorWheel existed yet (it's built
// lazily - see switchTab()) - applied once the wheel is constructed so it
// doesn't start pointed at its own default (center/white) instead of the
// device's actual current color.
let pendingBroadcastColor = null;

function scheduleReconnect() {
    if (reconnectTimer) return;
    const delay = nextReconnectDelayMs(reconnectAttempt);
    reconnectAttempt++;
    reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connectWebSocket();
    }, delay);
}

function connectWebSocket() {
    document.body.classList.add('ws-disconnected');

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    let socket;
    try {
        socket = new WebSocket(`${protocol}//${window.location.host}/ws`);
    } catch (e) {
        // A synchronous throw here (e.g. a blocked/invalid URL) previously killed the retry
        // chain permanently, since nothing downstream of `new WebSocket(...)` ever ran to
        // schedule the next attempt.
        scheduleReconnect();
        return;
    }
    ws = socket;

    // The stale-connection check above only starts once onopen has fired - a handshake that
    // never resolves either way (stuck in CONNECTING, no open/error/close) would otherwise
    // never be noticed at all. Force-closing a still-CONNECTING socket triggers its own
    // closing handshake and (per the WebSocket spec) eventually fires onclose, which is what
    // actually reschedules the reconnect below.
    const connectTimeout = setTimeout(() => {
        if (socket.readyState === WebSocket.CONNECTING) {
            try { socket.close(); } catch (e) { /* already closing/closed */ }
        }
    }, 10000);

    ws.onopen = () => {
        clearTimeout(connectTimeout);
        reconnectAttempt = 0;
        document.body.classList.remove('ws-disconnected');
        awaitingInitialTab = true;
        lastMessageAt = Date.now();
        if (staleCheckTimer) clearInterval(staleCheckTimer);
        staleCheckTimer = setInterval(() => {
            if (Date.now() - lastMessageAt > STALE_CONNECTION_MS) markConnectionStale();
        }, STALE_CONNECTION_MS);
    };
    ws.onerror = () => {
        // onclose still fires after onerror for a connection that got far enough to open, so
        // the reconnect scheduling stays there - this only matters for a failure early enough
        // that onclose might not (a defensive backstop, not the primary path).
        scheduleReconnect();
    };
    ws.onclose = () => {
        clearTimeout(connectTimeout);
        document.body.classList.add('ws-disconnected');
        if (staleCheckTimer) {
            clearInterval(staleCheckTimer);
            staleCheckTimer = null;
        }
        scheduleReconnect();
    };
    ws.onmessage = (event) => {
        lastMessageAt = Date.now();
        handleServerMessage(event.data);
    };
}

// Shows the panel for `tab` ('random' | 'effect' | 'color') and highlights
// its tab button. Never sends a command by itself - purely a local view
// switch, whether triggered by a click or by pickInitialTab() on connect.
function switchTab(tab) {
    document.querySelectorAll('.mode-tab').forEach((btn) => {
        btn.classList.toggle('active', btn.dataset.tab === tab);
    });
    document.querySelectorAll('.mode-panel').forEach((panel) => {
        panel.classList.toggle('active', panel.dataset.panel === tab);
    });

    if (tab === 'color' && !colorWheel) {
        colorWheel = new ColorWheel('colorWheel', 'colorSelector', 'colorPreview', 'colorInfo');
        if (pendingBroadcastColor) {
            colorWheel.setFromBroadcast(
                pendingBroadcastColor.r, pendingBroadcastColor.g, pendingBroadcastColor.b);
            pendingBroadcastColor = null;
        }
    }
}

// Updates the power button's DOM/label/dataset to reflect isOn, without
// sending any command - used both after a local click and when a state
// broadcast reports the device changed power state from elsewhere.
function applyPowerState(isOn) {
    deviceIsOn = isOn;
    const powerBtn = document.getElementById('powerBtn');
    if (!powerBtn) return;

    const { nextCmd, cssClass, label } = powerButtonState(isOn);
    powerBtn.dataset.cmd = nextCmd;
    powerBtn.classList.remove('on', 'off');
    powerBtn.classList.add(cssClass);
    powerBtn.title = label;
    powerBtn.setAttribute('aria-label', label);
}

// Updates the Random panel's play/pause icon button to reflect isHolding,
// without sending any command - used both after a local click and when a
// state broadcast reports the device changed hold state from elsewhere.
// This always shows the true holding state, regardless of *why* the device
// is holding (a plain Hold press, or a specific effect/color pinned from
// the other tabs) - see controls-logic.js's holdIcon().
function applyHoldState(isHolding) {
    deviceIsHolding = isHolding;
    const holdBtn = document.getElementById('holdBtn');
    if (!holdBtn) return;

    const { icon, nextCmd } = holdIcon(isHolding);
    holdBtn.dataset.cmd = nextCmd;
    holdBtn.classList.toggle('holding', isHolding);
    holdBtn.setAttribute('aria-label', icon === 'play' ? 'Resume rotation' : 'Pause rotation');
    holdBtn.title = icon === 'play' ? 'Resume' : 'Pause';
    holdBtn.querySelector('.icon').innerHTML = icon === 'play'
        ? '<path d="M8 5v14l11-7z"/>'
        : '<path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"/>';
}

// Handles a "state" message pushed by the server: on initial /ws connect,
// and again any time device state changes (including from another tab).
function handleServerMessage(raw) {
    let msg;
    try {
        msg = JSON.parse(raw);
    } catch (e) {
        return;
    }
    if (msg.type !== 'state') return;

    // A missing/malformed field here previously flowed straight into the DOM: `undefined`
    // power/holding rendered the wrong button state (and made its next click send the wrong
    // command), and `undefined` brightness snapped the slider to its own default with a
    // rgb(NaN, NaN, NaN) thumb color.
    const state = sanitizeStateMessage(msg);
    applyPowerState(state.power);
    applyHoldState(state.holding);

    const brightnessSlider = document.getElementById('brightnessSlider');
    if (brightnessSlider && !sliderActivelyDragging && state.brightness !== null) {
        brightnessSlider.value = state.brightness;
        updateSliderThumb(brightnessSlider);
    }

    if (msg.color && typeof msg.color.r === 'number' && typeof msg.color.g === 'number' &&
        typeof msg.color.b === 'number') {
        const { r, g, b } = msg.color;
        applyColorDisplay(r, g, b);
        if (colorWheel) colorWheel.setFromBroadcast(r, g, b);
        else pendingBroadcastColor = { r, g, b };
    }

    // Model selection and device name moved to their own pages (/advanced.html,
    // /device-name.html); the state message still carries msg.models / msg.model
    // / msg.device, this page just no longer renders them.

    const effectSelect = document.getElementById('effectSelect');
    if (effectSelect && Array.isArray(msg.effects)) {
        if (effectSelect.options.length !== msg.effects.length) {
            effectSelect.innerHTML = '';
            for (const effect of msg.effects) {
                const option = document.createElement('option');
                option.value = effect.id;
                option.textContent = effect.name;
                effectSelect.appendChild(option);
            }
        }
        // Keeps the dropdown's selection in sync with whatever effect is
        // actually running (however it got there), regardless of which tab
        // is currently visible - cheap, non-disruptive.
        const selectedId = findEffectIdByName(msg.effects, msg.effect);
        if (selectedId !== null) effectSelect.value = String(selectedId);
    }

    if (awaitingInitialTab) {
        awaitingInitialTab = false;
        switchTab(pickInitialTab(msg));
    }
}

// Initialize page
function initControlsPage() {
    const h1 = document.getElementById("logo");
    h1.style.setProperty('--grad', randomGradient());

    // Check if we're in AP mode
    const hostname = window.location.hostname;
    if (hostname === '192.168.4.1' || hostname.includes('192.168.4.')) {
        document.getElementById('apModeInfo').style.display = 'block';
    }

    // FPS display toggle
    const fpsEl = document.getElementById('fps');
    fpsEl.addEventListener('click', () => {
        fpsEl.classList.toggle('hidden');
    });

    // Mode tabs: Random / Effect / Color - purely local view switches, see
    // switchTab(). The tab that's active on load is later overridden once
    // by the first state broadcast's pickInitialTab() (handleServerMessage).
    document.querySelectorAll('.mode-tab').forEach((tabBtn) => {
        tabBtn.addEventListener('click', () => switchTab(tabBtn.dataset.tab));
    });

    // Effect dropdown: picking an option sends the effect command (which
    // implicitly holds it server-side - the Random tab's play/pause icon
    // will reflect that next state broadcast).
    document.getElementById('effectSelect').addEventListener('change', function () {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'effect', id: parseInt(this.value) }));
        }
    });

    // Shuffle buttons (Effect and Color panels): resume the random rotation
    // and switch back to the Random tab.
    ['effectShuffleBtn', 'colorShuffleBtn'].forEach((id) => {
        document.getElementById(id).addEventListener('click', () => {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({ type: 'resume' }));
            }
            switchTab('random');
        });
    });

    // RGB label <-> histogram toggle (histogram is the default view)
    const colorInfoToggle = document.getElementById('colorInfoToggle');
    colorInfoToggle.addEventListener('click', () => {
        colorInfoToggle.classList.toggle('showing-rgb');
    });

    // Settings toggle
    const settingsToggle = document.getElementById('settingsToggle');
    const settingsDrawer = document.getElementById('settingsDrawer');

    settingsToggle.addEventListener('click', () => {
        const isExpanded = settingsToggle.classList.contains('expanded');

        if (isExpanded) {
            settingsToggle.classList.remove('expanded');
            settingsDrawer.classList.remove('expanded');
        } else {
            settingsToggle.classList.add('expanded');
            settingsDrawer.classList.add('expanded');
        }
    });

    // Brightness slider (initial value comes from the state message pushed
    // right after the WebSocket connects - see handleServerMessage)
    const brightnessSlider = document.getElementById('brightnessSlider');

    updateSliderThumb(brightnessSlider);

    const startSliderDrag = () => { sliderActivelyDragging = true; };
    // Just clears the dragging flag - the actual commit is sent from the
    // 'change' listener below. mouseup/touchend and 'change' both fire on
    // release, so sending the commit from here too would double-send it.
    const stopSliderDrag = () => { sliderActivelyDragging = false; };
    // Sends one extra "commit" message when the drag ends, telling the
    // device to persist this value to NVS - the dozens/sec messages sent
    // during the drag itself (below) never do, to avoid hammering flash.
    const commitSliderValue = () => {
        sliderActivelyDragging = false;
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({
                type: 'brightness',
                value: parseInt(brightnessSlider.value),
                commit: true
            }));
        }
    };
    brightnessSlider.addEventListener('mousedown', startSliderDrag);
    brightnessSlider.addEventListener('touchstart', startSliderDrag);
    brightnessSlider.addEventListener('mouseup', stopSliderDrag);
    brightnessSlider.addEventListener('touchend', stopSliderDrag);
    brightnessSlider.addEventListener('change', commitSliderValue);

    brightnessSlider.addEventListener('input', function() {
        updateSliderThumb(this);
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({type: 'brightness', value: parseInt(this.value)}));
        }
    });

    const logo = document.getElementById('logo');

    // Button ripple effects
    document.querySelectorAll('.btn').forEach(btn => {
        btn.addEventListener('click', function(e) {
            e.preventDefault();
            const ripple = document.createElement('span');
            const rect = this.getBoundingClientRect();
            const size = Math.max(rect.width, rect.height);
            const x = e.clientX - rect.left - size / 2;
            const y = e.clientY - rect.top - size / 2;

            ripple.style.cssText = `
                position: absolute;
                width: ${size}px;
                height: ${size}px;
                left: ${x}px;
                top: ${y}px;
                background: rgba(255, 255, 255, 0.3);
                border-radius: 50%;
                transform: scale(0);
                animation: ripple 0.6s linear;
                pointer-events: none;
            `;
            this.appendChild(ripple);
            setTimeout(() => ripple.remove(), 600);

            if (this.classList.contains('off')) {
                logo.style.backgroundImage = `linear-gradient(135deg, #111 40%, #fff 50%, #111 60%)`;
                logo.style.animation = 'shine-move 4s linear infinite';
                logo.style.webkitTextFillColor = 'transparent';
                document.getElementById('logo').style.setProperty('--logo-filt', 0.1);
            }
            else if (this.classList.contains('on')) {
                logo.style.setProperty('--grad', randomGradient());
                logo.style.backgroundImage = '';
                logo.style.animation = '';
                logo.style.webkitTextFillColor = 'transparent';
                updateSliderThumb(brightnessSlider);
            }
            else if (this.classList.contains('next')) {
                logo.style.setProperty('--grad', randomGradient());
                logo.style.backgroundImage = '';
                logo.style.animation = '';
                logo.style.webkitTextFillColor = 'transparent';

                updateSliderThumb(brightnessSlider);
            }

            if (this.dataset.cmd && ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({ type: this.dataset.cmd }));
            }

            // Power/Hold buttons: toggle AFTER send so the correct cmd was sent
            if (this.id === 'powerBtn') {
                applyPowerState(!deviceIsOn);
            }
            if (this.id === 'holdBtn') {
                applyHoldState(!deviceIsHolding);
            }
        });
    });

    // Add dynamic CSS for slider thumb color
    const style = document.createElement('style');
    style.textContent = `
        .brightness-slider::-webkit-slider-thumb {
            background: var(--thumb-color, #ffffff) !important;
        }
        .brightness-slider::-moz-range-thumb {
            background: var(--thumb-color, #ffffff) !important;
        }
    `;
    document.head.appendChild(style);
}

// Start everything when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    document.body.classList.add('ws-disconnected');
    connectWebSocket();
    initControlsPage();
});