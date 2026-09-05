# Andromeda

Firmware for the Andromeda LED controller — an ESP32-based controller that drives
addressable LED strips through a library of animated effects, controlled from any
phone or laptop on your network through a small built-in web app. No app to
install, no account, no cloud: the device serves its own interface.

This isn't general-purpose LED controller software — it only runs on this
project's own line of purpose-built devices, not general LED devices.

## Installing

The easiest way to flash a board is the **[web installer](https://ippo343.github.io/andromeda/)**.
Open it in desktop Chrome, Edge, or Opera (Web Serial isn't available in Firefox,
Safari, or on mobile), plug the board in over USB, pick your device model, and hit
install. It always serves the newest stable release.

## First-time setup

A device with no saved Wi-Fi credentials starts its own **setup network**. The
first strip glows blue while it's trying to connect, then briefly green on
success; three red flashes mean it had credentials but couldn't get on the
network.

1. On your phone or laptop, join the open Wi-Fi network named after the device —
   something like `Andromeda-A1B2` or `L10-A1B2`.
2. A setup page should open by itself. If it doesn't, browse to
   **http://192.168.4.1**.
3. Tap **Scan for Networks**, pick yours, enter the password, and save.
4. The device reboots onto your network. Reconnect your phone to that same
   network and open **http://andromeda.local**.

You can control the lights straight from the setup network too, without ever
joining it to Wi-Fi — the controls are on the same page.

## Using it

Open **http://andromeda.local** (the device also answers to its own name, e.g.
`http://l10-a1b2.local`). Everything is on one screen:

- **Random** — the default. Effects cycle on their own; pause to hold the current
  one, or skip to the next.
- **Effect** — pick one effect by name and stay on it.
- **Color** — pick a single static colour from the wheel.
- **Brightness** — the slider at the bottom, live as you drag.
- **Power** — the round button turns the lights off and back on.

Under **Settings** you'll find:

- **Advanced** — live diagnostics, firmware version and updates, device model,
  reboot, and a link to the device logs.
- **Device Name** — rename the device; the name is also its network address.
- **WiFi Setup** — move it to a different network, or clear the saved credentials.

## Updates

The device checks for a new firmware release once a day and shows a badge on the
**Advanced** page when one is available. Nothing installs by itself — press
**Update now** when you want it. Tick *Receive dev / beta updates* to opt into
pre-release builds; leave it off to stay on stable.

Keep the power on while an update runs. If it fails partway through, the device
simply stays on its current firmware — check the Advanced page for the error
and try again.

## Troubleshooting

**`andromeda.local` doesn't resolve.** Some networks and older Android versions
don't do mDNS. Find the device's IP in your router's client list and use that
directly. If several Andromeda devices share a network, use each one's own name
(shown on the WiFi Setup page) rather than the shared `andromeda.local`.

**The device is broadcasting its setup network again.** It couldn't reach the
saved Wi-Fi — wrong password, network renamed, router out of range, or the router
was rebooting. Join the setup network and re-enter the credentials.

**The page loads but nothing responds.** The controls grey out when the
connection to the device drops; they come back on their own once it reconnects.
If they don't, reload the page, then power-cycle the device.

**The lights flash red three times at boot.** Wi-Fi credentials are saved but the
network couldn't be joined. The device keeps running effects — join its setup
network to fix the credentials.

**The web page is blank or missing after an interrupted update.** The device
repairs its own interface: leave it powered on and connected to Wi-Fi, and it
re-downloads the web app on its next update check.

**Guest / captive-portal networks.** Devices on many guest networks are isolated
from each other, so your phone can't reach the controller. Put both on the normal
network.

**Nothing lights up at all.** Check the strip's power supply first, then that the
data line is on the pin your model expects, then re-flash with the web installer
and make sure you pick the right device model.

**Start over completely.** WiFi Setup → clear the saved settings; the device
reboots into setup mode with its lights and effects untouched.

## Building from source

This is a [PlatformIO](https://platformio.org/) project — `pio run -e <board>` to
build, `pio run -e <board> -t upload -t uploadfs` to flash. See `platformio.ini`
for the available board environments and `CLAUDE.md` for the developer notes.

## License

See [LICENSE](LICENSE). Third-party licences are listed in the footer of the
device's own web interface.
