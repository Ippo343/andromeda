// Shared utility functions

// Generate random HSL color
function randomHSL() {
    const hue = Math.floor(Math.random() * 360);
    const sat = 100;
    const light = 50;
    return `hsl(${hue}, ${sat}%, ${light}%)`;
}

// Generate random gradient for logo
function randomGradient() {
    const steps = 3 + Math.floor(Math.random() * 4);
    const colors = Array.from({ length: steps }, randomHSL);
    colors.push(colors[0]);

    const stopPercent = 50 / (colors.length - 1);
    const stops = colors
        .map((c, i) => `${c} ${(i * stopPercent).toFixed(1)}%`)
        .join(', ');

    return `repeating-linear-gradient(to bottom right, ${stops})`;
}

// Escape HTML to prevent XSS
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}