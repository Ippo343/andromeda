// Shared Web Serial helpers for the installer page's post-flash steps: setting the model
// (model-select.js) and the recovery console (console.js, issue #135) both need to reopen
// the port esp-web-tools just flashed and read lines back from it. Pulled out once a second
// caller needed the exact same reconnect/read-lines dance - see each file's own header
// comment for why the DOM + Web Serial glue itself stays untested (mirrors
// installer.js/controls.js: the DOM-free logic worth testing lives in *-logic.js instead).

// The port used for flashing is opened and closed entirely inside
// <esp-web-install-button>'s own closure - it isn't exposed anywhere.
// navigator.serial.getPorts() returns previously-authorized SerialPort objects without
// prompting again, so if exactly one is already authorized (the normal case: one board, just
// flashed) this reconnects silently. Anything else (none yet, or more than one from an
// earlier visit) falls back to requestPort(), which does prompt - safer than guessing at
// which physical port to write to. That fallback can itself throw SecurityError if there's no
// live user gesture on the call stack - callers need their own click handler to satisfy that.
async function getTargetPort() {
    const authorized = await navigator.serial.getPorts();
    if (authorized.length === 1) return authorized[0];
    return navigator.serial.requestPort();
}

// Reads lines until `predicate` matches one, or timeoutMs elapses. reader.read() has no
// built-in timeout - a device that never sends anything would otherwise hang this forever -
// so a timer forces it to resolve via reader.cancel(), which makes the pending read()
// resolve with done:true. That renders the reader unusable for any further read() (the whole
// point: a real timeout is a terminal, not a recoverable, condition here), which is why this
// is only ever called once per port session per caller.
async function waitForLine(reader, predicate, timeoutMs) {
    const decoder = new TextDecoder();
    let buffer = '';
    const timer = setTimeout(() => {
        reader.cancel().catch(() => {});
    }, timeoutMs);
    try {
        while (true) {
            const { value, done } = await reader.read();
            if (done) return false;
            buffer += decoder.decode(value, { stream: true });
            let newlineAt;
            while ((newlineAt = buffer.indexOf('\n')) !== -1) {
                const line = buffer.slice(0, newlineAt);
                buffer = buffer.slice(newlineAt + 1);
                if (predicate(line)) return true;
            }
        }
    } finally {
        clearTimeout(timer);
    }
}
