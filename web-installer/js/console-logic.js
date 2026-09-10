// DOM-free logic for the recovery console drawer (issue #135) - a last-resort way to read
// back a device's .local addresses over USB when nothing else worked. Pulled out the same
// way model-select-logic.js is; console.js does the DOM/Web Serial wiring and stays
// untested, mirroring model-select.js.

// The exact newline-terminated JSON line main.cpp's processSerialCommands() recognizes
// (handled before WsCommandParser::parse(), since it's a serial-only diagnostic - see that
// function's comment).
function buildNetInfoCommand() {
    return '{"type":"netinfo"}\n';
}

// buildRebootCommand() lives in model-select-logic.js - the console reuses it verbatim
// rather than duplicating the same one-line JSON command.

// Bounded ring buffer for the console's log lines: a chatty or looping device must not grow
// the backing array (and the DOM it feeds) without limit. Appends `line`, dropping the
// oldest entries once over `maxLines`. Returns a new array rather than mutating `lines`, so
// callers can just reassign (`lines = appendLogLine(lines, text, cap)`).
function appendLogLine(lines, line, maxLines) {
    const next = lines.concat([line]);
    if (next.length <= maxLines) return next;
    return next.slice(next.length - maxLines);
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { buildNetInfoCommand, appendLogLine };
}
