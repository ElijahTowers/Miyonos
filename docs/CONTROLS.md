# Controls

The Onion key map was verified against the selected SDL2 port and OnionOS
source. All screens use semantic actions rather than raw SDL keys.

| Miyoo control | Now Playing | Lists and menus |
|---|---|---|
| D-pad Up/Down | Selected target volume ± configured step | Move one row |
| D-pad Left/Right | Previous / next track | Move one page |
| A | Play/pause | Open or activate |
| B | Back | Back/cancel |
| X | Toggle group mute | Context action where shown |
| Y | Rooms & Groups | Rooms & Groups |
| L1 / R1 | Previous / next speaker or Group volume | Jump 20 rows |
| L2 | Queue | Queue |
| R2 | Favorites | Favorites |
| Start | Main Menu | Main Menu |
| Select | Controls pop-up | Controls pop-up |
| Menu | Exit confirmation | Exit confirmation |

In **Queue**, X opens **Favorite Playlists**. Queue reads Sonos' `Q:0` track
container, so it shows the actual upcoming tracks rather than technical queue
instances. Each visible Queue row shows its own source-provided cover-art
thumbnail when available; covers are fetched lazily, one at a time, and use
the existing bounded artwork cache. **Favorite Playlists** filters the playlist-shaped Favorites that
Sonos exposes, including Spotify playlists; X returns to Queue. The selected
playlist's cover art appears beside its name; A replaces the active Sonos
queue with that playlist and immediately opens Now Playing with the selected
playlist name, then begins at its first track. The playlist name remains on
Now Playing while that queue advances through later tracks. Its cover and name
also remain in the dedicated lower-right playlist block, separate from the
current track cover.

In **Favorites**, the selected item appears beside its source-provided cover
art. Folders and providers that do not expose a usable cover show **Cover
unavailable** instead. When enabled, supported Sonos Radio/TuneIn station
logos use the same bounded cache. L1/R1 always cycles Group together with
the visible individual speakers; Up/Down changes the currently shown target.

For Spotify Favorites and supported Sonos Radio stations that provide only a
public HTTPS cover, turn on **Settings → External cover art over HTTPS**. This
default-off setting is limited to verified Spotify and Sonos Radio/TuneIn cover
URLs and never uses a Sonos or music-service login.

Press **Select** at any time for a compact pop-up that shows every physical
button and its current assignment. Press A, B, or Select again to close it.
This is also the quickest way to check a custom layout.

Every physical control can be reassigned under **Settings → Button Mapping**.
Available actions include navigation, play/pause, mute/context, rooms,
previous/next speaker, previous/next track, seek backward/forward, Queue,
Favorites, Main Menu, Controls, Refresh, Exit, and No action. Changes remain staged until
B is pressed and the layout passes its safety check. Up, Down, Confirm, Back,
and Exit must each remain assigned to at least one button.

The on-screen control guide and the table above describe the default layout.
The Button Mapping screen always describes editing actions semantically, so it
remains usable even after the physical A, B, X, or Select buttons are changed.

As a mapping-independent recovery path, hold **Menu + Start for three
seconds**. Miyonos restores every default button and returns to Settings. This
fixed chord cannot be reassigned or disabled.

Held D-pad input starts with the operating system's repeat delay and is then
limited to one accepted repeat every 160 ms. Volume retains its conservative
configured step and pending network updates are coalesced.

In the IPv4 editor, Left/Right selects an octet, Up/Down changes it, A validates
and saves, B cancels, and X clears saved manual addresses.

In the group editor, A joins an ungrouped visible room or removes a
non-coordinator member. Miyonos refuses to remove the final member and will not
remove the active coordinator through that action.

Desktop alternatives are: arrows; Z or Space for A; X or Left Ctrl for B; A or
Left Shift for X; S or Left Alt for Y; Q/E for L1; W/T for R1; 1/Tab for L2;
2/Backspace for R2; Return for Start; R or Right Ctrl for Select; Escape for
Menu.

When **Diagnostics mode** is enabled, the Diagnostics screen displays the most
recent raw SDL key code. This provides a small on-device verification tool if a
future Onion/SDL port changes the software mapping.
