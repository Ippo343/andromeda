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
        const brightness = value / 255;
        logo.style.setProperty('--logo-filt', brightness);
    }
}

// Color Wheel Class
class ColorWheel {
    constructor(canvasId, selectorId, previewId, infoId) {
        this.canvas = document.getElementById(canvasId);
        this.selector = document.getElementById(selectorId);
        this.preview = document.getElementById(previewId);
        this.info = document.getElementById(infoId);

        this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
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

                    const rgb = this.hsvToRgb(hue, saturation, 1);

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

    hsvToRgb(h, s, v) {
        const c = v * s;
        const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
        const m = v - c;

        let r = 0, g = 0, b = 0;

        if (h >= 0 && h < 60) { r = c; g = x; b = 0; }
        else if (h >= 60 && h < 120) { r = x; g = c; b = 0; }
        else if (h >= 120 && h < 180) { r = 0; g = c; b = x; }
        else if (h >= 180 && h < 240) { r = 0; g = x; b = c; }
        else if (h >= 240 && h < 300) { r = x; g = 0; b = c; }
        else if (h >= 300 && h < 360) { r = c; g = 0; b = x; }

        return [
            Math.round((r + m) * 255),
            Math.round((g + m) * 255),
            Math.round((b + m) * 255)
        ];
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
        const rect = this.canvas.getBoundingClientRect();

        let clientX, clientY;
        if (e.touches) {
            clientX = e.touches[0].clientX;
            clientY = e.touches[0].clientY;
        } else {
            clientX = e.clientX;
            clientY = e.clientY;
        }

        const x = clientX - rect.left;
        const y = clientY - rect.top;

        const dx = x - this.centerX;
        const dy = y - this.centerY;
        const distance = Math.sqrt(dx * dx + dy * dy);

        if (distance <= this.radius) {
            this.updateColor(x, y);
        }
    }

    updateColor(x, y) {
        this.selector.style.left = `${x}px`;
        this.selector.style.top = `${y}px`;

        const imageData = this.ctx.getImageData(x, y, 1, 1).data;
        const r = imageData[0];
        const g = imageData[1];
        const b = imageData[2];

        const colorString = `rgb(${r}, ${g}, ${b})`;

        this.selector.style.backgroundColor = colorString;
        this.preview.style.backgroundColor = colorString;

        this.info.innerHTML =
            `<span style="color: #ff6b6b">${r}</span>, ` +
            `<span style="color: #6bff6b">${g}</span>, ` +
            `<span style="color: #6b6bff">${b}</span>`;

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

        this.sendColor(r, g, b);
    }

    sendColor(r, g, b) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({type: 'color', r, g, b}));
        }
    }
}

// WebSocket management
let ws = null;

// Power button toggle state - module scope so both the click handler and
// incoming state broadcasts (handleServerMessage) can read/update it.
let deviceIsOn = true;

// True while the brightness slider is actively being dragged, so an
// incoming state broadcast (e.g. triggered from another tab) doesn't yank
// the slider out from under the user mid-drag.
let sliderActivelyDragging = false;

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}/ws`);
    ws.onopen = () => document.body.classList.remove('ws-disconnected');
    ws.onclose = () => {
        document.body.classList.add('ws-disconnected');
        setTimeout(connectWebSocket, 2000);
    };
    ws.onmessage = (event) => handleServerMessage(event.data);
}

// Updates the power button's DOM/label/dataset to reflect isOn, without
// sending any command - used both after a local click and when a state
// broadcast reports the device changed power state from elsewhere.
function applyPowerState(isOn) {
    deviceIsOn = isOn;
    const powerBtn = document.getElementById('powerBtn');
    if (!powerBtn) return;

    if (isOn) {
        powerBtn.textContent = 'Off';
        powerBtn.dataset.cmd = 'power_off';
        powerBtn.classList.remove('on');
        powerBtn.classList.add('off');
    } else {
        powerBtn.textContent = 'On';
        powerBtn.dataset.cmd = 'power_on';
        powerBtn.classList.remove('off');
        powerBtn.classList.add('on');
    }
}

// Shows/hides the "model change needs a reboot to take effect" indicator -
// used both by the model select's own change handler and by state
// broadcasts (so it's correct on load, before anyone has touched anything).
function setRebootIndicator(show) {
    const rebootBtn = document.getElementById('rebootBtn');
    const modelRow = document.getElementById('modelRow');
    if (!rebootBtn || !modelRow) return;

    if (show) {
        rebootBtn.classList.add('visible');
        modelRow.classList.add('has-reboot');
    } else {
        rebootBtn.classList.remove('visible');
        modelRow.classList.remove('has-reboot');
    }
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

    applyPowerState(msg.power);

    const brightnessSlider = document.getElementById('brightnessSlider');
    if (brightnessSlider && !sliderActivelyDragging) {
        brightnessSlider.value = msg.brightness;
        updateSliderThumb(brightnessSlider);
    }

    const colorPreview = document.getElementById('colorPreview');
    const colorInfo = document.getElementById('colorInfo');
    if (colorPreview && colorInfo && msg.color) {
        const { r, g, b } = msg.color;
        colorPreview.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
        colorInfo.innerHTML =
            `<span style="color: #ff6b6b">${r}</span>, ` +
            `<span style="color: #6bff6b">${g}</span>, ` +
            `<span style="color: #6b6bff">${b}</span>`;
    }

    const modelSelect = document.getElementById('modelSelect');
    if (modelSelect && Array.isArray(msg.models)) {
        modelSelect.innerHTML = '';
        for (const model of msg.models) {
            const option = document.createElement('option');
            option.value = model.id;
            option.textContent = model.name;
            modelSelect.appendChild(option);
        }
        if (msg.model) modelSelect.value = String(msg.model.configured.id);
    }
    if (msg.model) setRebootIndicator(msg.model.rebootRequired);
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

    // Color Picker toggle
    const colorPickerToggle = document.getElementById('colorPickerToggle');
    const colorWheelDrawer = document.getElementById('colorWheelDrawer');
    let colorWheel = null;

    colorPickerToggle.addEventListener('click', () => {
        const isExpanded = colorWheelDrawer.classList.contains('expanded');

        if (isExpanded) {
            colorWheelDrawer.classList.remove('expanded');
        } else {
            colorWheelDrawer.classList.add('expanded');

            if (!colorWheel) {
                colorWheel = new ColorWheel('colorWheel', 'colorSelector', 'colorPreview', 'colorInfo');
            }
        }
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
    const stopSliderDrag = () => { sliderActivelyDragging = false; };
    brightnessSlider.addEventListener('mousedown', startSliderDrag);
    brightnessSlider.addEventListener('touchstart', startSliderDrag);
    brightnessSlider.addEventListener('mouseup', stopSliderDrag);
    brightnessSlider.addEventListener('touchend', stopSliderDrag);
    brightnessSlider.addEventListener('change', stopSliderDrag);

    brightnessSlider.addEventListener('input', function() {
        updateSliderThumb(this);
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({type: 'brightness', value: parseInt(this.value)}));
        }
    });

    const modelSelect = document.getElementById('modelSelect');
    const logo = document.getElementById('logo');

    // Model Selection Listener
    modelSelect.addEventListener('change', function() {
        const modelId = parseInt(this.value);

        this.style.borderColor = 'rgba(255, 255, 255, 0.8)';
        setTimeout(() => {
            this.style.borderColor = 'rgba(255, 255, 255, 0.1)';
        }, 400);

        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({
                type: 'model',
                id: modelId
            }));
        }

        setRebootIndicator(true);
    });

    document.getElementById('rebootBtn').addEventListener('click', function() {
        this.innerHTML = "Rebooting...";
        this.style.opacity = "0.5";

        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'reboot' }));
        }

        setTimeout(() => location.reload(), 4000);
    });

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

            // Power button: toggle AFTER send so the correct cmd was sent
            if (this.id === 'powerBtn') {
                applyPowerState(!deviceIsOn);
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