// Thin DOM glue over installer-logic.js - fetches version.json (written by
// build-scripts/assemble_site.py) and fills in the page. No WebSocket, no
// device state; the actual flashing is all handled by <esp-web-install-button>,
// slots and all - see index.html's unsupported/not-allowed slots.

(function () {
    // Same animated-gradient logo as the on-device pages - see
    // data/js/controls.js's initControlsPage(). utils.js (copied in by
    // assemble_site.py) defines randomGradient(); without setting --grad,
    // .logo's background-clip: text has nothing to clip and renders invisible.
    document.getElementById('logo').style.setProperty('--grad', randomGradient());

    fetch('version.json')
        .then((res) => res.json())
        .then((versionInfo) => {
            const versionEl = document.getElementById('version');
            // Built with DOM APIs, not innerHTML: versionInfo.tag/releaseUrl
            // trace back to the pushed git tag name, which isn't restricted
            // from containing HTML - a tag like `v1<img src=x onerror=...>`
            // landing in a real release must not turn into script execution
            // on this public page. textContent/setAttribute never parse
            // their input as markup, so this is safe regardless of what the
            // tag contains. See test_web_installer.cpp's XSS guard.
            versionEl.textContent = '';
            versionEl.append('Installing ');
            const tagEl = document.createElement('strong');
            tagEl.textContent = versionInfo.tag;
            versionEl.append(tagEl);
            if (versionInfo.releaseUrl) {
                versionEl.append(' · ');
                const link = document.createElement('a');
                link.href = versionInfo.releaseUrl;
                link.target = '_blank';
                link.rel = 'noopener';
                link.textContent = 'release notes';
                versionEl.append(link);
            }
        })
        .catch(() => {
            document.getElementById('version').textContent =
                'Could not load release info - reload the page.';
        });
})();
