// Thin DOM + Web Serial glue for the recovery console drawer (issue #135) - untested,
// mirroring model-select.js/installer.js; the DOM-free logic it calls (console-logic.js) is
// what's tested. A last-resort recovery path: reads the boot/live log a device already
// prints over USB, and offers Reboot / Network info as one-click sends of commands the
// firmware already accepts (see main.cpp's processSerialCommands(), which is exactly what
// model-select.js already uses for "set model").
//
// Deliberately independent of model-select.js's own port session (that one is scoped to
// right after a flash and closes itself down) - this can be opened at any time, including
// with no flash having happened this visit at all.

(function () {
    const BAUD_RATE = 115200;
    const MAX_LOG_LINES = 500;

    const details = document.getElementById('console-drawer');
    const logEl = document.getElementById('console-log');
    const connectBtn = document.getElementById('console-connect');
    const rebootBtn = document.getElementById('console-reboot');
    const netinfoBtn = document.getElementById('console-netinfo');
    if (!details || !logEl || !connectBtn || !rebootBtn || !netinfoBtn) return;

    let port = null;
    let reader = null;
    let writer = null;
    let readLoopPromise = null;
    let lines = [];
    // Guards connect() against a rapid double-click on Connect - without it, two overlapping
    // calls would each open their own port and reader/writer, and the second call's
    // assignments would silently leak the first's (mirrors model-select.js's `inProgress`).
    let connecting = false;

    function render() {
        logEl.textContent = lines.join('\n');
        logEl.scrollTop = logEl.scrollHeight;
    }

    function appendLine(line) {
        lines = appendLogLine(lines, line, MAX_LOG_LINES);
        render();
    }

    function setConnected(connected) {
        connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
        rebootBtn.disabled = !connected;
        netinfoBtn.disabled = !connected;
    }

    // Runs until the port closes or a read errors out - unlike model-select.js's
    // waitForLine(), this has no single expected line to stop at, so it just keeps decoding
    // until told to stop (disconnect() cancels the reader, which ends the loop the same way
    // waitForLine()'s timeout does).
    async function readLoop() {
        const decoder = new TextDecoder();
        let buffer = '';
        try {
            while (true) {
                const { value, done } = await reader.read();
                if (done) break;
                buffer += decoder.decode(value, { stream: true });
                let newlineAt;
                while ((newlineAt = buffer.indexOf('\n')) !== -1) {
                    appendLine(buffer.slice(0, newlineAt));
                    buffer = buffer.slice(newlineAt + 1);
                }
            }
        } catch (err) {
            appendLine(`[console closed: ${err && err.message ? err.message : err}]`);
        }
    }

    async function disconnect() {
        try {
            if (reader) await reader.cancel().catch(() => {});
        } finally {
            reader = null;
        }
        if (readLoopPromise) await readLoopPromise.catch(() => {});
        readLoopPromise = null;
        try {
            if (writer) writer.releaseLock();
        } catch (_) {
            // Already released/broken - nothing more to do.
        }
        writer = null;
        if (port) {
            try {
                await port.close();
            } catch (_) {
                // Already closed/broken - nothing more to do.
            }
        }
        port = null;
        setConnected(false);
    }

    async function connect() {
        if (connecting || port) return;
        connecting = true;
        try {
            port = await getTargetPort();
            await port.open({ baudRate: BAUD_RATE });
            reader = port.readable.getReader();
            writer = port.writable.getWriter();
            readLoopPromise = readLoop();
            setConnected(true);
            appendLine('[connected]');
        } catch (err) {
            appendLine(`[could not connect: ${err && err.message ? err.message : err}]`);
            port = null;
        } finally {
            connecting = false;
        }
    }

    async function send(command) {
        if (!writer) return;
        const encoder = new TextEncoder();
        await writer.write(encoder.encode(command)).catch((err) => {
            appendLine(`[write failed: ${err && err.message ? err.message : err}]`);
        });
    }

    connectBtn.addEventListener('click', () => {
        if (port) {
            disconnect();
        } else {
            connect();
        }
    });
    rebootBtn.addEventListener('click', () => send(buildRebootCommand()));
    netinfoBtn.addEventListener('click', () => send(buildNetInfoCommand()));

    // Hand the port back to esp-web-tools if the user clicks Connect & install while a
    // console session still owns it (issue #244). Otherwise esp-web-tools' own port.open()
    // for flashing hits an already-open port and the install dead-ends in an error dialog
    // with nothing pointing at this (collapsed) drawer as the cause. The listener is on the
    // capture phase so it runs before <esp-web-install-button>'s own click handler kicks off
    // the flash flow; the close is async and can't hold up the synchronous click, but
    // esp-web-tools only opens the port after the user has picked it from the browser's port
    // chooser - far longer than port.close() takes. The button lives in light DOM and exists
    // before the custom element upgrades, so this query is safe here (model-select.js relies
    // on the same).
    const installActivateBtn = document.querySelector('esp-web-install-button [slot="activate"]');
    if (installActivateBtn) {
        installActivateBtn.addEventListener('click', () => {
            if (port) {
                appendLine('[disconnected for install]');
                disconnect();
            }
        }, true);
    }

    setConnected(false);
})();
