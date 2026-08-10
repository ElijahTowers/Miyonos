MIYONOS 0.1.31 TECHNICAL PREVIEW

INSTALL FROM AN SD CARD

1. Shut down the Miyoo Mini Plus and remove its OnionOS SD card.
2. Extract the release ZIP directly onto the root of the SD card.
3. Confirm that /App/Miyonos/config.json exists on the card.
4. Safely eject the card, insert it, and start the Miyoo.
5. Open Apps, then Miyonos.

Miyonos stores settings, logs, and artwork only in App/Miyonos/data.
Installing an update over the app does not intentionally remove that folder.
External cover art over HTTPS is optional and off by default. It can be enabled
from Settings after installation; Sonos control itself remains local.

If Miyonos immediately returns to the menu, see App/Miyonos/data/logs.
If launch.sh lost its executable bit during a network copy, run:
  chmod +x /mnt/SDCARD/App/Miyonos/launch.sh

UNIVERSAL BROWSER INSTALL OR UPDATE

On the Miyoo, enable Apps > Tweaks > Network > HTTP: Web-based file sync.
From any Windows, macOS, or Linux browser, open the Miyoo IP address, log in
with admin / admin, open App, and upload the complete Miyonos folder from the
universal browser package.

No software is installed on the computer. The same upload updates Miyonos
while preserving the data directory.

ADVANCED SSH INSTALL OR UPDATE

Enable SSH in Apps > Tweaks > Network > SSH, note the Miyoo IP address, then
keep the release ZIP, its .sha256 file, and Miyonos-wifi-install.sh together
on your computer. Run:
  ./Miyonos-wifi-install.sh MIYOO_IP_ADDRESS

The same command performs future updates while preserving the data directory.
The updater can also be run from the source project at scripts/wifi-install.sh.
See docs/WIFI_INSTALL.md in the source project for rollback and login details.

Miyonos is an independent community project and is not affiliated with or
endorsed by Sonos, Inc. or the OnionOS project.
