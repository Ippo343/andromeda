// Static-analysis test, not a runtime one: tools/native-bridge/server.js
// only serves data/ files it explicitly lists in STATIC_ROUTES (mirroring
// comms.cpp's STATIC_FILE_ROUTE list, which test_static_assets.cpp already
// guards against index.html drifting out of sync with) - a <script>/<link>
// tag added to data/index.html without a matching STATIC_ROUTES entry would
// otherwise silently 404 only in the native visualizer, with every other
// test green, exactly the gap test_static_assets.cpp's own header describes
// having shipped once for comms.cpp. This closes the same gap for the
// bridge's hand-maintained copy of that route list.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..');

function referencedLocalPaths(html) {
    const paths = new Set();
    const attrRe = /(?:src|href)="([^"]+)"/g;
    let match;
    while ((match = attrRe.exec(html)) !== null) {
        let value = match[1];
        if (!value || value.startsWith('#')) continue;
        if (value.startsWith('http://') || value.startsWith('https://')) continue;
        if (!value.startsWith('/')) value = '/' + value;
        paths.add(value);
    }
    return paths;
}

function staticRouteKeys(serverJsSource) {
    // Matches each quoted object key immediately followed by ": {" inside
    // the STATIC_ROUTES literal, e.g. '/js/controls.js': { file: ... }.
    const routeRe = /'([^']+)':\s*\{\s*file:/g;
    const routes = new Set();
    let match;
    while ((match = routeRe.exec(serverJsSource)) !== null) routes.add(match[1]);
    return routes;
}

test('every data/index.html local asset has a route in the native bridge', () => {
    const html = fs.readFileSync(path.join(REPO_ROOT, 'data', 'index.html'), 'utf8');
    const serverJs = fs.readFileSync(
        path.join(REPO_ROOT, 'tools', 'native-bridge', 'server.js'), 'utf8');

    const referenced = referencedLocalPaths(html);
    const registered = staticRouteKeys(serverJs);

    for (const p of referenced) {
        assert.ok(
            registered.has(p),
            `${p} is referenced by data/index.html but tools/native-bridge/server.js has no ` +
            'STATIC_ROUTES entry for it - it will 404 in the native visualizer setup'
        );
    }
});
