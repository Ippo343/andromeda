// Thin DOM + Web Serial glue over model-select-logic.js - see that file's
// header for the "why" of this whole step. Untested, mirroring
// installer.js/controls.js - the DOM-free logic it calls is what's tested.
// getTargetPort()/waitForLine() live in serial.js, shared with console.js
// (issue #135) - loaded before this script, see index.html.
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
    // How often the MODEL command is re-sent while waiting for an ack (see
    // the comment at the write site below) - frequent enough that a WROOM's
    // post-reset boot window (well under ACK_TIMEOUT_MS) gets several
    // chances, infrequent enough not to spam the UART.
    const RETRY_INTERVAL_MS = 2000;
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

    // getTargetPort()/waitForLine(): serial.js. Note for this call site specifically:
    // getTargetPort()'s requestPort() fallback can throw SecurityError here - it needs an
    // active user gesture, and the page's only one was already spent on the button's own
    // click, long before this MutationObserver callback runs - see the catch below for how
    // that's surfaced instead of the flow just breaking silently.

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
            const modelCommand = encoder.encode(buildModelCommand(modelId));

            // Sent right away, not after waiting for a boot marker - but
            // also *repeated* every RETRY_INTERVAL_MS until acked, rather
            // than sent once. On a board with native USB CDC (S3/C3, see
            // platformio.ini's ARDUINO_USB_CDC_ON_BOOT), opening the port
            // doesn't reset the chip, so the first write almost always
            // lands and acks well inside one retry interval. On a WROOM
            // (external USB-UART bridge), opening the port asserts
            // DTR/RTS and hard-resets the chip (#281): a write sent the
            // instant port.open() resolves races that reset and can be
            // lost entirely - during the ROM bootloader's ownership of the
            // UART, or before Serial.begin() (main.cpp's
            // initSerialAndFilesystem(), first thing in setup()) has even
            // run - with no way to know it happened. Retrying periodically
            // means some write eventually lands after Serial.begin() and
            // loop() are both up and draining the UART (main.cpp), well
            // within ACK_TIMEOUT_MS even accounting for setup()'s
            // multi-second WiFi-connect blocking phase before loop()
            // starts. Safe to repeat: WsCommandParser's MODEL handling
            // just re-persists the same id and re-logs the same ack line.
            setStatus('Setting model…');
            await writer.write(modelCommand);
            const retryTimer = setInterval(() => {
                writer.write(modelCommand).catch(() => {});
            }, RETRY_INTERVAL_MS);
            let accepted;
            try {
                accepted = await waitForLine(
                    reader,
                    (line) => line.includes('Factory config: Set model ID'),
                    ACK_TIMEOUT_MS
                );
            } finally {
                clearInterval(retryTimer);
            }

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
