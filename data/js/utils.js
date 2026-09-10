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
