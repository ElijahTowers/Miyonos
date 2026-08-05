# Universal browser installation

The recommended Miyonos installation uses OnionOS's built-in HTTP file server.
It works from Windows, macOS, or Linux without installing a local helper,
runtime, terminal tool, browser extension, or driver. See the
[official OnionOS HTTP file server documentation](https://onionui.github.io/docs/4.2/network/http)
for the device-side feature.

## Prepare the package

1. Extract `Miyonos-0.1.21-Universal-Browser-Installer.zip`.
2. Keep the complete extracted folder together.
3. Double-click `Open Miyonos Installer.html`.

The guide is a self-contained local HTML file. It deliberately works from a
`file://` address and has no external scripts, fonts, analytics, or network
requests.

## Enable the Miyoo file server

On the Miyoo Mini Plus:

1. Open **Apps → Tweaks → Network → HTTP: Web-based file sync**.
2. Enable the service and authentication.
3. Note the IP address at the top of the Network screen.
4. Exit Tweaks so the service starts.

Enter that IP address in the Miyonos guide. The button opens the website hosted
by the Miyoo itself. The default OnionOS HTTP login is:

```text
Username: admin
Password: admin
```

Use this only on a trusted private network. Change the default password if the
service will remain enabled.

## Upload Miyonos

In the OnionOS file server:

1. Open the `App` directory.
2. Choose upload.
3. Select the complete `Miyonos` folder included beside the HTML guide.
4. For an update, approve merging or replacing existing application files.
5. Wait for every file to finish uploading before closing the browser.

The release contains no `data` directory. Uploading an update therefore leaves
settings, logs, diagnostics, and cached artwork in place. Do not run Miyonos
while an update is being uploaded.

Afterward, disable HTTP file sync if it is no longer needed and open
**Apps → Miyonos**. A first installation may require an Apps refresh or MainUI
restart.

## Browser security boundary

A separate website cannot silently select local files, bypass the Miyoo login,
or make arbitrary SSH connections. Those restrictions protect the computer and
local network. The universal guide therefore opens the file server hosted by
the Miyoo, where the user explicitly selects the included application folder.

For automated staging, checksum verification, backup, and rollback, use the
optional SSH flow in [WIFI_INSTALL.md](WIFI_INSTALL.md).
