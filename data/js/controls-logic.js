// Pure, DOM-free logic pulled out of controls.js so it's testable under
// Node (node --test test/js) without a browser/jsdom - mirrors how
// ws-command-parser.h/ws-state-builder.h were pulled out of comms.cpp as
// pure, natively-testable headers on the firmware side. Loaded via a plain
// <script> tag before controls.js (same global-scope pattern as
// utils.js/controls.js - no bundler), and required directly by tests via
// the module.exports guard at the bottom.

// Which mode tab should be active right after a WS connection's first state
// message. Only "color" is derived from state - everything else defaults to
// "random" and stays there until the user clicks a tab or a shuffle button;
// see controls.js's handleServerMessage for why this is only evaluated once
// per connect rather than on every subsequent state message.
function pickInitialTab(state) {
    return state && state.color && state.color.active ? 'color' : 'random';
}

// Finds the id of the effect named `name` in the `effects` list (the
// {id, name} pairs from the state broadcast), or null if there's no match -
// used to keep the Effect dropdown's selected option in sync with the
// currently running effect's name.
function findEffectIdByName(effects, name) {
    if (!Array.isArray(effects)) return null;
    const found = effects.find((e) => e.name === name);
    return found ? found.id : null;
}

// Drives the Random panel's play/pause icon-toggle button: what icon to
// show and what command a click should send next, given the current
// `holding` state.
function holdIcon(holding) {
    return holding ? { icon: 'play', nextCmd: 'resume' } : { icon: 'pause', nextCmd: 'hold' };
}

// Drives the small power toggle button: its label/cssClass always describe
// the action a click will perform next (matching the existing .btn.off/.btn.on
// color convention), not the device's current power state.
function powerButtonState(isOn) {
    return isOn
        ? { nextCmd: 'power_off', cssClass: 'off', label: 'Turn off' }
        : { nextCmd: 'power_on', cssClass: 'on', label: 'Turn on' };
}

// Factors out the repeated `(x / 255) * 100` from controls.js's
// applyColorDisplay(), used to size the RGB histogram bars.
function rgbToHistogramWidths(r, g, b) {
    return {
        r: (r / 255) * 100,
        g: (g / 255) * 100,
        b: (b / 255) * 100,
    };
}

// Converts an HSV color (h in degrees [0, 360), s and v in [0, 1]) to RGB
// bytes [0, 255]. Extracted from ColorWheel.hsvToRgb (unchanged behavior).
function hsvToRgb(h, s, v) {
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
        Math.round((b + m) * 255),
    ];
}

// Maps a brightness slider value [0, 255] to the logo's CSS
// filter: brightness() multiplier. The logo is a visual nicety, not an
// indicator of the real brightness sent to the controller - users mostly
// live near the top of the slider, so a steep curve (exponent << 1) keeps
// the logo looking bright well below max and only dims sharply near zero.
function logoBrightnessFilter(value) {
    return Math.pow(value / 255, 0.35);
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        pickInitialTab,
        findEffectIdByName,
        holdIcon,
        powerButtonState,
        rgbToHistogramWidths,
        hsvToRgb,
        logoBrightnessFilter,
    };
}
