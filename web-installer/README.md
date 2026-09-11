# `web-installer/` — browser firmware flasher

The source for `https://ippo343.github.io/andromeda/` — flashes a board over
Web Serial (desktop Chrome / Edge / Opera) with no toolchain, using
[ESP Web Tools](https://esphome.github.io/esp-web-tools/). Always serves the
newest **stable** release.

## Layout — same logic/wiring split as `data/`

| wiring (`*.js`, untested) | logic (`*-logic.js`, unit-tested) | purpose |
|---|---|---|
| `installer.js` | `installer-logic.js` | fetch `version.json`, render the page, run ESP Web Tools |
| `model-select.js` | `model-select-logic.js` | post-flash "set model" step — sends a `MODEL` command over the same serial link right after flashing (#105/#187) |
| `console.js` | `console-logic.js` | recovery console drawer — read back a device's `.local` addresses over USB when nothing else worked (#135) |
| `serial.js` | — | shared Web Serial helpers for the two post-flash steps |
| `models.js` | — | the model list the "set model" step offers — a hand-maintained mirror of `include/geometry/model_config.h`, guarded by `test/test_native_suite/test_installer_model_list.cpp` |

`*-logic.test.js` files live in [`../test/js/`](../test/js/); run with
`npm run test:web`.

## Build & deploy

- `esp-web-tools` is loaded at an **exact pinned version**, never `@latest` —
  an upstream change would silently alter how strangers flash boards.
  `test/test_native_suite/test_web_installer.cpp` guards the pin.
- `build-scripts/assemble_site.py` assembles the deployable site: this tree +
  the released boot parts + `css/common.css` / `js/utils.js` copied out of
  `data/` (so the installer matches the on-device UI).
- `.github/workflows/release.yml` syncs these sources into the release repo
  (`Ippo343/andromeda`); that repo's `pages.yml` runs `assemble_site.py`
  against the newest stable release's assets and deploys to GitHub Pages.
- The pipeline (offset validation, manifest shape, styling) is exercised on
  **every** commit by `test.yml`'s `web-installer-assemble` job — no release
  needed.

Full chain: [`../docs/release-pipeline.md`](../docs/release-pipeline.md).
