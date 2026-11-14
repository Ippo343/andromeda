// Shared utility functions

// Generate random HSL color
function randomHSL() {
    const hue = Math.floor(Math.random() * 360);
    const sat = 100;
    const light = 50;
    return `hsl(${hue}, ${sat}%, ${light}%)`;
}

// A random fully-saturated colour-stop list for the logo gradient. Split out of
// randomGradient() so a caller can reuse the exact same palette at a different
// angle - the OTA progress bar (#211) re-projects it flat and steep while the
// logo keeps its diagonal.
function randomGradientStops() {
    const steps = 3 + Math.floor(Math.random() * 4);
    const colors = Array.from({ length: steps }, randomHSL);
    colors.push(colors[0]);

    const stopPercent = 50 / (colors.length - 1);
    return colors
        .map((c, i) => `${c} ${(i * stopPercent).toFixed(1)}%`)
        .join(', ');
}

// Generate a random gradient for the logo. `direction` defaults to the logo's
// diagonal; pass a `<angle>deg` string to tilt it. `stops` defaults to a fresh
// random palette; pass one from randomGradientStops() to match another gradient.
function randomGradient(direction = 'to bottom right', stops = randomGradientStops()) {
    return `repeating-linear-gradient(${direction}, ${stops})`;
}

// Escape HTML to prevent XSS
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// fetch() with an abort timeout (ms). AbortController is optional - older
// browsers and parts of the jsdom harness may not have it, in which case the
// request is simply not time-limited rather than failing to start.
function fetchWithTimeout(url, options = {}, timeoutMs = 5000) {
    if (typeof AbortController === 'undefined') return fetch(url, options);
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    return fetch(url, { ...options, signal: controller.signal }).finally(() => clearTimeout(timeout));
}

// Show/hide the "needs a reboot to take effect" button next to a row. The
// element ids stay with the view; the class toggle is identical everywhere.
function setRebootIndicator(rowId, buttonId, show) {
    const row = document.getElementById(rowId);
    const btn = document.getElementById(buttonId);
    if (!row || !btn) return;
    row.classList.toggle('has-reboot', show);
    btn.classList.toggle('visible', show);
}

// Paint a colour preview box, its RGB readout and the three histogram bars.
// `ids` names the five elements; widths come from controls-logic.js's
// rgbToHistogramWidths so the controls and rule drawers cannot disagree.
function applyRgbPreview(ids, r, g, b) {
    const preview = document.getElementById(ids.preview);
    if (preview) preview.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;

    const info = document.getElementById(ids.info);
    if (info) {
        info.innerHTML =
            `<span style="color: #ff6b6b">${r}</span>, ` +
            `<span style="color: #6bff6b">${g}</span>, ` +
            `<span style="color: #6b6bff">${b}</span>`;
    }

    const widths = rgbToHistogramWidths(r, g, b);
    const bars = [
        [ids.histR, widths.r],
        [ids.histG, widths.g],
        [ids.histB, widths.b],
    ];
    for (const [id, width] of bars) {
        const el = document.getElementById(id);
        if (el) el.style.width = `${width}%`;
    }
}
