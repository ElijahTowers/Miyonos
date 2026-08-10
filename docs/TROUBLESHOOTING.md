# Troubleshooting

## No Sonos rooms found

Confirm the Miyoo has Wi-Fi, the speakers and Miyoo are on the same IPv4 LAN,
and neither is isolated on a guest network. Select **Search Again**. If
multicast is blocked, open **Settings → Manual player IP**, enter the IPv4
address of any visible Sonos player with the D-pad, and press A. Miyonos can
discover the rest of the topology from that player.

The app does not need internet access for Sonos control. Internet failure only
affects the music source itself and, if explicitly enabled, Spotify cover art;
LAN control continues.

## The app exits immediately or shows black

First confirm that `App/Miyonos/VERSION` contains `0.1.21` or newer. Earlier
releases could initialize successfully but leave the physical Miyoo Mini Plus
framebuffer black, blurred, or incomplete. Version 0.1.16 avoids the
unreliable Mini hardware blitter and writes sharp 640 × 480 frames directly to the
double-buffered device framebuffer.

Press Menu twice to exit safely if the interface is ever invisible. The first
press opens the exit confirmation; the second confirms it and lets the launcher
resume Onion MainUI.

Check:

```text
/mnt/SDCARD/App/Miyonos/data/logs/launcher.log
/mnt/SDCARD/App/Miyonos/data/logs/miyonos.log
```

Verify the ZIP was extracted at the SD root, `config.json` is directly below
`App/Miyonos`, and both `launch.sh` and `miyonos` are executable. Do not run the
ARM binary on a desktop. If logs report a missing library, reinstall the full
release ZIP so `App/Miyonos/libs` is complete.

## Playback or rooms look stale

Use the main menu or the optional Refresh button mapping to refresh. Group
changes made by another controller can take one
poll interval to appear. After repeated failures Miyonos refreshes topology
and retries discovery with backoff. A changed player address is replaced in
the cache after successful discovery; use a current manual IP if needed.

## Artwork is missing

Playback control is independent of artwork. Version 0.1.21 uses metadata from
both Sonos playback calls and bundles the JPEG decoder needed for typical Sonos
`/getaa` covers. The source may still expose no art, an unreachable URL, or an
unsupported/oversized image. Confirm **Automatic artwork** is on. Try **Clear
artwork cache** if a card write was interrupted. **Cover unavailable** is the
expected, explicit state whenever no valid bounded image is available,
including many TV, line-in, and radio sources.

Some Spotify Favorites and Sonos Radio stations provide public artwork rather
than a local Sonos image. To fetch those optional covers, first turn on
**Settings → External cover art over HTTPS**. The setting is off by default and
does not use or ask for any Sonos or music-service login. Version 0.1.21
supports the strict real-world Spotify and Spotify-in-Sonos endpoint forms,
plus three fixed Sonos Radio proxies and one verified redirect to either fixed
TuneIn logo CDN. It includes the three offline root certificates required by
those fixed hosts. Check that Wi-Fi and the Miyoo's system time are correct:
certificate verification intentionally refuses a connection when the clock is
far wrong. If it remains unavailable, leave the switch off;
the player and the rest of Miyonos continue to work locally.

## A radio station first shows an error, then plays

Version 0.1.21 waits for a direct Sonos Radio station to finish its bounded
buffering transition before deciding whether it started. It no longer shows a
temporary UPnP 701 result while the station is becoming available. If it still
reports that the station did not begin playback, the speaker did not confirm
the selected station within eight seconds; try it once in the Sonos app and
then refresh Miyonos.

## Battery level is unavailable

The battery indicator appears only on Now Playing. It is read from the Miyoo
Mini Plus hardware locally and never from the network. If the fuel gauge does
not respond, Miyonos hides the indicator rather than showing a guessed value.

## Diagnostics

Open **Main Menu → Diagnostics** to refresh state, clear local logs, or export
`App/Miyonos/data/miyonos-diagnostics.txt`. The export contains version,
network status, counts, last error, and cache size, but no listening history or
unique player identifiers. Enabling **Diagnostics mode** increases bounded
local protocol logging; nothing is uploaded.

If Onion MainUI does not return after a manually killed launch, run
`killall -CONT MainUI` over SSH, then attach `launcher.log` to a bug report.
The packaged launcher traps normal termination and performs this resume
automatically.
