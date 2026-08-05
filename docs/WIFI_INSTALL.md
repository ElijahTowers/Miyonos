# Advanced SSH installation and update

For the recommended no-install method that works on Windows, macOS, and Linux,
use [UNIVERSAL_INSTALL.md](UNIVERSAL_INSTALL.md). The SSH method below is an
optional advanced route that adds staged switching, checksum verification,
automatic backup, and rollback.

Miyonos can be installed and updated from a Mac, Linux PC, or Windows machine
with Bash/WSL while the Miyoo stays on. The updater uses OnionOS's encrypted
SSH service; it does not require FTP, Samba, or removing the SD card.

## Easiest option on a Mac: use the browser

1. Unzip `Miyonos-0.1.21-WiFi-Installer-macOS.zip`.
2. Keep the extracted folder together and double-click
   `Open Miyonos Installer.command`.
3. A local Miyonos page opens in the default browser.
4. Enter the Miyoo IP address and click **Install or update**.

The page is bound to `127.0.0.1`, so other devices cannot open it. It validates
same-origin requests with a random per-launch token. The Onion password is
passed only to the local SSH process for the current operation and is neither
stored nor sent to the internet. The page also exposes the safe rollback
operation behind a separate confirmation.

This method requires Python 3 on the Mac. The current Miyonos development
MacBook already provides it. The command-line method below remains available.

## One-time OnionOS setup

1. Connect the Miyoo Mini Plus and computer to the same trusted Wi-Fi network.
2. On the Miyoo, open **Apps → Tweaks → Network → SSH: Secure shell**.
3. Enable SSH and note the IP address shown at the top of the Network screen.
4. With SSH authentication enabled, OnionOS uses username `onion` and password
   `onion`. If you deliberately disable authentication, use `root` with no
   password and set `MIYOO_USER=root` below.

Do not leave SSH enabled on public or untrusted Wi-Fi.

## First install from the release files

Keep these three files together in one folder:

- `Miyonos-wifi-install.sh`
- `Miyonos-0.1.21-OnionOS.zip`
- `Miyonos-0.1.21-OnionOS.zip.sha256`

Then run:

```sh
chmod +x Miyonos-wifi-install.sh
./Miyonos-wifi-install.sh 192.168.1.50
```

## First install from the source project

From the Miyonos project folder:

```sh
./scripts/wifi-install.sh 192.168.1.50
```

Replace the example address with the Miyoo's displayed IP. Accept the SSH host
key on the first connection and enter the Onion password when requested. Open
**Apps → Miyonos** afterward. A first install may require refreshing Apps or
restarting the Miyoo menu.

## Update

Replace the ZIP and checksum beside the standalone updater, then run the same
command:

```sh
./Miyonos-wifi-install.sh 192.168.1.50
```

The updater verifies the ZIP checksum before connecting, uploads into a hidden
staging directory, stops Miyonos if it is running, moves the complete `data`
directory into the new install, and only then switches versions. Settings,
logs, diagnostics, and cached artwork are preserved.

The previous app version is retained in a hidden backup directory without
duplicating user data. Restore it with:

```sh
./Miyonos-wifi-install.sh --rollback 192.168.1.50
```

Set `MIYOO_KEEP_BACKUP=0` to remove the previous binary immediately after a
successful update. Other supported overrides are:

- `MIYOO_SSH_MULTIPLEX=0` disables SSH connection reuse for older or limited
  OnionOS SSH servers. Use it when login succeeds but a later updater step
  waits indefinitely.

```sh
MIYOO_USER=root \
MIYOO_APP_PATH=/mnt/SDCARD/App/Miyonos \
./Miyonos-wifi-install.sh 192.168.1.50
```

The older `scripts/deploy.sh` command remains as an alias to the same safe
updater.
