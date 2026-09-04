// Thin DOM + Web Serial glue over model-select-logic.js - see that file's
// header for the "why" of this whole step. Untested, mirroring
// installer.js/controls.js - the DOM-free logic it calls is what's tested.
//
// No separate "set model" button: the model picker is required *before*
// "Connect & install" is even clickable, and once flashing finishes this
// script sets it automatically. <esp-web-install-button>'s pinned version
// (see index.html's comment) exposes no public "install finished" event -
// its flashing UI is a <ewt-install-dialog> it creates and appends to
// document.body itself, with no supported hook out. A MutationObserver on
// document.body watching for that element's removal is the only reliable,
// version-stable signal available: it fires whether the dialog closed via
// success, an error, or the user cancelling, so the follow-up here has to
// (and does, via its own ack-timeout) cope with "nothing is actually there".
//
// Uses the plain Web Serial API directly, not esp-web-tools - no
// esptool-js, no raw flash/NVS writes, just writing bytes to the same UART
// the device already reads commands from.

(function () {
    const BAUD_RATE = 115200;
    // Generous: setup() can block for several seconds (WiFi connect attempt,
    // boot animations) before loop() starts draining serial - see main.cpp.
    const ACK_TIMEOUT_MS = 15000;
    // <ewt-install-dialog> closes itself right after telling the device to
    // reboot into the new firmware; give the OS a moment to actually free
    // the port before this script tries to reopen it.
    const REOPEN_DELAY_MS = 500;

    const select = document.getElementById('model-select');
    const installButton = document.querySelector('esp-web-install-button [slot="activate"]');
    const statusEl = document.getElementById('model-status');
    if (!select || !installButton || !statusEl) return;

    for (const opt of buildModelOptions(MODELS)) {
        const el = document.createElement('option');
        el.value = opt.value;
        el.textContent = opt.label;
        select.append(el);
    }

    // Connect & install stays disabled until a real model is picked - the
    // placeholder option has an empty value, so a device is never flashed
    // with whatever the dropdown happened to preselect (index.html also
    // marks the <select> itself required).
    select.addEventListener('change', () => {
        installButton.disabled = !select.value;
    });

    function setStatus(text) {
        statusEl.textContent = text;
    }

    // Reads lines until `predicate` matches one, or timeoutMs elapses.
    // reader.read() has no built-in timeout - a device that never sends
    // anything would otherwise hang this forever - so a timer forces it to
    // resolve via reader.cancel(), which makes the pending read() resolve
    // with done:true. That renders the reader unusable for any further
    // read() (the whole point: a real timeout is a terminal, not a
    // recoverable, condition here), which is why this function is only ever
    // called once per port session - see the single call site below.
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

    // The port used for flashing is opened and closed entirely inside
    // <esp-web-install-button>'s own closure - it isn't exposed anywhere.
    // navigator.serial.getPorts() returns previously-authorized SerialPort
    // objects without prompting again, so if exactly one is already
    // authorized (the normal case: one board, just flashed) this reconnects
    // silently. Anything else (none yet, or more than one from an earlier
    // visit) falls back to requestPort(), which does prompt - safer than
    // guessing at which physical port to write to. That fallback can itself
    // throw SecurityError here: requestPort() needs an active user gesture,
    // and the page's only one was already spent on the button's own click,
    // long before this MutationObserver callback runs - see the catch below
    // for how that's surfaced instead of the flow just breaking silently.
    async function getTargetPort() {
        const authorized = await navigator.serial.getPorts();
        if (authorized.length === 1) return authorized[0];
        return navigator.serial.requestPort();
    }

    let inProgress = false;

    async function setModelAndReboot() {
        if (inProgress || !select.value) return;
        inProgress = true;
        const modelId = Number(select.value);

        let port, reader, writer;
        try {
            setStatus('Reconnecting to set the model…');
            await new Promise((resolve) => setTimeout(resolve, REOPEN_DELAY_MS));
            port = await getTargetPort();
            await port.open({ baudRate: BAUD_RATE });

            writer = port.writable.getWriter();
            reader = port.readable.getReader();
            const encoder = new TextEncoder();

            // Sent right away, not after waiting for a boot marker: on a
            // board with native USB CDC (S3/C3, see platformio.ini's
            // ARDUINO_USB_CDC_ON_BOOT), opening the port doesn't reset the
            // chip, so it may already be well past its startup banner by
            // now. Bytes written before the device's own loop() starts
            // draining serial sit safely in its UART driver's RX buffer
            // either way (main.cpp), so sending immediately works regardless
            // of exactly when the device booted.
            setStatus('Setting model…');
            await writer.write(encoder.encode(buildModelCommand(modelId)));
            const accepted = await waitForLine(
                reader,
                (line) => line.includes('Factory config: Set model ID'),
                ACK_TIMEOUT_MS
            );

            if (!accepted) {
                setStatus('No acknowledgement from the device - check the connection and set '
                    + 'the model from its own Advanced Settings page instead.');
            } else {
                setStatus('Model set - rebooting…');
                await writer.write(encoder.encode(buildRebootCommand()));
                setStatus('Done. The device is rebooting with the new model.');
            }
        } catch (err) {
            if (err && err.name === 'NotFoundError') {
                setStatus('No port selected - set the model from the device\'s own Advanced '
                    + 'Settings page instead.');
            } else if (err && err.name === 'SecurityError') {
                // See getTargetPort()'s comment - only reachable when
                // getPorts() didn't find exactly one already-authorized
                // port, so requestPort() had no live user gesture to work
                // with (rare: normally getPorts() alone finds the board
                // <esp-web-install-button> just flashed).
                setStatus('Could not reconnect automatically - set the model from the device\'s '
                    + 'own Advanced Settings page instead.');
            } else {
                setStatus(`Could not set the model: ${err && err.message ? err.message : err}`);
            }
        } finally {
            // Locks must be released before port.close() will succeed - on
            // every path, not just the happy one, since a stray held lock
            // there throws and leaves the port unusable until the page
            // reloads. reader may already be effectively closed (a timeout
            // cancelled it above); releaseLock() on that is still safe.
            try {
                if (reader) reader.releaseLock();
            } catch (_) {
                // Already released/broken - nothing more to do.
            }
            try {
                if (writer) writer.releaseLock();
            } catch (_) {
                // Already released/broken - nothing more to do.
            }
            if (port) {
                try {
                    await port.close();
                } catch (_) {
                    // Already closed/broken - nothing more to do.
                }
            }
            inProgress = false;
        }
    }

    // <esp-web-install-button> appends <ewt-install-dialog> straight to
    // document.body when clicked, and removes it when the user closes it
    // (success, error, or cancel alike - see the header comment above).
    new MutationObserver((mutations) => {
        for (const mutation of mutations) {
            for (const node of mutation.removedNodes) {
                if (node.nodeName && node.nodeName.toLowerCase() === 'ewt-install-dialog') {
                    setModelAndReboot();
                    return;
                }
            }
        }
    }).observe(document.body, { childList: true });
})();
