#!/usr/bin/env python3
"""Small stateful Sonos/UPnP fixture server for Miyonos desktop tests.

This is a development dependency only; it is never included as a handheld
runtime requirement.
"""

import argparse
import base64
import html
import http.server
import re
import socketserver
import time
from urllib.parse import urlparse


AV = "urn:schemas-upnp-org:service:AVTransport:1"
RC = "urn:schemas-upnp-org:service:RenderingControl:1"
GRC = "urn:schemas-upnp-org:service:GroupRenderingControl:1"
ZGT = "urn:schemas-upnp-org:service:ZoneGroupTopology:1"
CD = "urn:schemas-upnp-org:service:ContentDirectory:1"


def service(service_type, service_id, control):
    return f"""<service><serviceType>{service_type}</serviceType>
<serviceId>urn:upnp-org:serviceId:{service_id}</serviceId>
<controlURL>{control}</controlURL><eventSubURL>{control}/Event</eventSubURL>
<SCPDURL>/xml/{service_id}1.xml</SCPDURL></service>"""


def envelope(action, namespace, fields=""):
    return (
        '<?xml version="1.0"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">'
        f'<s:Body><u:{action}Response xmlns:u="{namespace}">{fields}'
        f"</u:{action}Response></s:Body></s:Envelope>"
    )


def fault(code=701, description="Transition not available"):
    return (
        '<?xml version="1.0"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">'
        "<s:Body><s:Fault><faultcode>s:Client</faultcode>"
        "<faultstring>UPnPError</faultstring><detail>"
        '<UPnPError xmlns="urn:schemas-upnp-org:control-1-0">'
        f"<errorCode>{code}</errorCode><errorDescription>{description}</errorDescription>"
        "</UPnPError></detail></s:Fault></s:Body></s:Envelope>"
    )


def didl_item(identifier, title, artist, album, uri, duration="0:03:42"):
    return (
        f'<item id="{identifier}" parentID="Q:0" restricted="true">'
        f"<dc:title>{html.escape(title)}</dc:title>"
        f"<dc:creator>{html.escape(artist)}</dc:creator>"
        f"<upnp:album>{html.escape(album)}</upnp:album>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        "<upnp:albumArtURI>/getaa?s=1&amp;u=mock</upnp:albumArtURI>"
        f'<res duration="{duration}">{html.escape(uri)}</res></item>'
    )


def didl_playlist_track(playlist_id, identifier):
    prefix = "Road Trip Track" if playlist_id == 2 else "Mock Track"
    album = "Road Trip Playlist" if playlist_id == 2 else "Weekend Playlist"
    return didl_item(
        str(identifier),
        f"{prefix} {identifier}",
        "Miyonos Ensemble",
        album,
        f"http://127.0.0.1/audio/{identifier}.mp3",
    ).replace('parentID="Q:0"', f'parentID="SQ:{playlist_id}"')


def didl_saved_playlist(identifier):
    title = "Road Trip Playlist" if identifier == 2 else "Weekend Playlist"
    return (
        f'<container id="SQ:{identifier}" parentID="SQ:">'
        f"<dc:title>{title}</dc:title>"
        f"<upnp:albumArtURI>/getaa?s=1&amp;u=mock-{identifier}</upnp:albumArtURI>"
        "<upnp:class>object.container.playlistContainer</upnp:class>"
        f"<res>file:///jffs/settings/savedqueues.rsq#{identifier}</res></container>"
    )


def didl_generic_queue():
    return (
        '<item id="Q:0" parentID="Q:" restricted="true">'
        "<dc:title>Queue</dc:title>"
        "<upnp:class>object.item.audioItem</upnp:class>"
        "<res>x-rincon-queue:RINCON_LIVING#0</res></item>"
    )


def saved_playlist_id(uri):
    match = re.search(r"savedqueues\.rsq#([12])$", uri)
    return int(match.group(1)) if match else 0


class State:
    playing = True
    volume = 28
    muted = False
    shuffle = False
    elapsed = 77
    current_uri = "x-rincon-queue:RINCON_LIVING#0"
    scenario = "grouped"
    queue_cleared = False
    loaded_playlist_id = 0
    loaded_from_favorite = False
    coordinator_changed = False
    radio_transition = False
    radio_transition_started = False


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "MiyonosMock/0.1"

    def log_message(self, message, *args):
        if self.server.verbose:
            super().log_message(message, *args)

    def send_xml(self, body, status=200):
        encoded = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", 'text/xml; charset="utf-8"')
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self):
        if self.path == "/__simulator__/health":
            self.send_xml(f"<scenario>{State.scenario}</scenario>")
            return
        if self.path == "/delay":
            time.sleep(1.0)
            self.send_xml("<delayed/>")
            return
        if self.path.startswith("/getaa"):
            if State.scenario == "no-artwork":
                self.send_error(404)
                return
            data = base64.b64decode(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII="
            )
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        descriptions = {
            "/xml/device_description.xml": ("Living Room", "RINCON_LIVING"),
            "/xml/kitchen_device_description.xml": ("Kitchen", "RINCON_KITCHEN"),
            "/xml/right_device_description.xml": ("Right", "RINCON_RIGHT"),
            "/xml/sub_device_description.xml": ("Sub", "RINCON_SUB"),
        }
        if self.path not in descriptions:
            self.send_error(404)
            return
        host, port = self.server.server_address
        room_name, uuid = descriptions[self.path]
        body = f"""<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
<URLBase>http://127.0.0.1:{port}</URLBase>
<device><deviceType>urn:schemas-upnp-org:device:ZonePlayer:1</deviceType>
<friendlyName>{room_name}</friendlyName><roomName>{room_name}</roomName>
<modelName>Sonos Mock</modelName><modelNumber>M1</modelNumber>
<serialNum>00-00-00-00-00-01</serialNum><softwareVersion>99.0</softwareVersion>
<UDN>uuid:{uuid}</UDN><serviceList>
{service(AV, "AVTransport", "/MediaRenderer/AVTransport/Control")}
{service(RC, "RenderingControl", "/MediaRenderer/RenderingControl/Control")}
{service(GRC, "GroupRenderingControl", "/MediaRenderer/GroupRenderingControl/Control")}
{service(ZGT, "ZoneGroupTopology", "/ZoneGroupTopology/Control")}
{service(CD, "ContentDirectory", "/MediaServer/ContentDirectory/Control")}
</serviceList></device></root>"""
        self.send_xml(body)

    def action(self):
        header = self.headers.get("SOAPACTION", "")
        match = re.search(r"#([^\"']+)", header)
        return match.group(1) if match else ""

    def field(self, body, name, fallback=""):
        match = re.search(fr"<{name}>(.*?)</{name}>", body, re.S)
        return html.unescape(match.group(1)) if match else fallback

    def topology(self):
        scenario = State.scenario
        coordinator = (
            "RINCON_KITCHEN"
            if scenario == "coordinator-change" and State.coordinator_changed
            else "RINCON_LIVING"
        )
        living = (
            '<ZoneGroupMember UUID="RINCON_LIVING" ZoneName="Living Room" '
            'Location="http://127.0.0.1:1400/xml/device_description.xml" '
            'Invisible="0"/>'
        )
        kitchen = (
            '<ZoneGroupMember UUID="RINCON_KITCHEN" ZoneName="Kitchen" '
            'Location="http://127.0.0.1:1400/xml/kitchen_device_description.xml" '
            'Invisible="0"/>'
        )
        if scenario in ("one", "normal"):
            state = (
                '<ZoneGroups><ZoneGroup Coordinator="RINCON_LIVING" '
                'ID="RINCON_LIVING:1">'
                + living
                + "</ZoneGroup></ZoneGroups>"
            )
        elif scenario == "multi-room":
            state = (
                '<ZoneGroups><ZoneGroup Coordinator="RINCON_LIVING" '
                'ID="RINCON_LIVING:1">'
                + living
                + '</ZoneGroup><ZoneGroup Coordinator="RINCON_KITCHEN" '
                'ID="RINCON_KITCHEN:2">'
                + kitchen
                + "</ZoneGroup></ZoneGroups>"
            )
        else:
            members = (
                '<ZoneGroupMember UUID="RINCON_LIVING" ZoneName="Living Room" '
                'Location="http://127.0.0.1:1400/xml/device_description.xml" '
                'Invisible="0">'
            )
            if scenario in ("stereo", "home-theater"):
                members += '<Satellite UUID="RINCON_RIGHT" HTChanMapSet="R"/>'
            members += "</ZoneGroupMember>"
            members += kitchen
            if scenario == "home-theater":
                members += (
                    '<ZoneGroupMember UUID="RINCON_SUB" ZoneName="Sub" '
                    'Location="http://127.0.0.1:1400/xml/sub_device_description.xml" '
                    'Invisible="1"/>'
                )
            state = (
                '<ZoneGroups><ZoneGroup Coordinator="'
                + coordinator
                + '" ID="RINCON_LIVING:1">'
                + members
                + "</ZoneGroup></ZoneGroups>"
            )
        return envelope(
            "GetZoneGroupState", ZGT, f"<ZoneGroupState>{html.escape(state)}</ZoneGroupState>"
        )

    def browse(self, body):
        object_id = self.field(body, "ObjectID", "Q:")
        start = max(0, int(self.field(body, "StartingIndex", "0")))
        requested = min(100, max(1, int(self.field(body, "RequestedCount", "60"))))
        if State.scenario == "malformed":
            return envelope("Browse", CD, "<Result>&lt;broken&gt;</Result>")
        if object_id.startswith("Q"):
            queue_length = (
                8
                if State.loaded_playlist_id
                else 360 if State.scenario == "long-queue" else 135
            )
            all_items = [
                (
                    didl_playlist_track(State.loaded_playlist_id, index)
                    if State.loaded_playlist_id
                    else didl_item(
                        str(index),
                        f"Mock Track {index}",
                        "Miyonos Ensemble",
                        "Local Fixtures",
                        f"http://127.0.0.1/audio/{index}.mp3",
                    )
                )
                for index in range(1, queue_length + 1)
            ]
            total = len(all_items)
            items = "".join(all_items[start : start + requested])
        elif object_id in ("SQ:1", "SQ:2"):
            playlist_id = 2 if object_id == "SQ:2" else 1
            all_items = [
                didl_playlist_track(playlist_id, index)
                for index in range(1, 9)
            ]
            total = len(all_items)
            items = "".join(all_items[start : start + requested])
        elif object_id == "FV:2":
            items = (
                '<container id="FV:2/1" parentID="FV:2">'
                "<dc:title>Morning Collection</dc:title>"
                "<upnp:class>object.container</upnp:class></container>"
                + didl_item(
                    "FV:2/2",
                    "Independent Radio",
                    "Local Radio",
                    "",
                    "x-sonosapi-stream:station",
                    "0:00:00",
                )
                + didl_item(
                    "FV:2/3",
                    "Road Trip Playlist",
                    "Miyonos Ensemble",
                    "",
                    "x-rincon-cpcontainer:1006206cspotify%3Aplaylist%3Aroad-trip",
                    "0:00:00",
                )
            )
            total = 3
        elif object_id == "FV:2/1":
            items = didl_item(
                "FV:2/1/1",
                "Nested Favorite",
                "Miyonos Ensemble",
                "Local Fixtures",
                "x-rincon-cpcontainer:1006206cmock",
            )
            total = 1
        elif object_id == "SQ:":
            items = didl_saved_playlist(1) + didl_saved_playlist(2)
            total = 2
        else:
            items = ""
            total = 0
        didl = (
            '<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" '
            'xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/" '
            'xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/">'
            + items
            + "</DIDL-Lite>"
        )
        count = len(re.findall(r"<(?:item|container) ", items))
        fields = (
            f"<Result>{html.escape(didl)}</Result><NumberReturned>{count}</NumberReturned>"
            f"<TotalMatches>{total}</TotalMatches><UpdateID>7</UpdateID>"
        )
        return envelope("Browse", CD, fields)

    def do_POST(self):
        length = min(int(self.headers.get("Content-Length", "0")), 2 * 1024 * 1024)
        body = self.rfile.read(length).decode("utf-8", "replace")
        action = self.action()
        if State.scenario in ("delay", "slow"):
            time.sleep(1.0)
        if State.scenario == "fault" and action not in ("GetZoneGroupState",):
            self.send_xml(fault(), 500)
            return
        if action == "GetZoneGroupState":
            response = self.topology()
            if State.scenario == "coordinator-change":
                State.coordinator_changed = True
        elif action == "GetTransportInfo":
            if State.radio_transition:
                State.radio_transition = False
                State.playing = True
            state = "PLAYING" if State.playing else "PAUSED_PLAYBACK"
            response = envelope(
                action,
                AV,
                f"<CurrentTransportState>{state}</CurrentTransportState>"
                "<CurrentTransportStatus>OK</CurrentTransportStatus>"
                "<CurrentSpeed>1</CurrentSpeed>",
            )
        elif action == "GetPositionInfo":
            didl = (
                '<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" '
                'xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/" '
                'xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/">'
                + didl_item(
                    "1",
                    "A Local Song & Test",
                    "Miyonos Ensemble",
                    "Protocol Fixtures",
                    State.current_uri,
                )
                + "</DIDL-Lite>"
            )
            response = envelope(
                action,
                AV,
                "<Track>1</Track><TrackDuration>0:03:42</TrackDuration>"
                f"<TrackMetaData>{html.escape(didl)}</TrackMetaData>"
                f"<TrackURI>{html.escape(State.current_uri)}</TrackURI>"
                f"<RelTime>0:01:{State.elapsed % 60:02d}</RelTime>"
                "<AbsTime>NOT_IMPLEMENTED</AbsTime><RelCount>2147483647</RelCount>"
                "<AbsCount>2147483647</AbsCount>",
            )
        elif action == "GetMediaInfo":
            # Sonos Radio preserves the station identity but rewrites its
            # per-session flags after a stream becomes active.
            reported_uri = State.current_uri
            if reported_uri == "x-sonosapi-stream:transitioning":
                reported_uri += "?sid=303&flags=8232&sn=4"
            playlist = (
                '<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" '
                'xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/" '
                'xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/">'
                + (didl_generic_queue()
                   if State.loaded_from_favorite
                   else didl_saved_playlist(State.loaded_playlist_id or 1))
                + "</DIDL-Lite>"
            )
            response = envelope(
                action,
                AV,
                f"<NrTracks>8</NrTracks><MediaDuration>0:29:00</MediaDuration>"
                f"<CurrentURI>{html.escape(reported_uri)}</CurrentURI>"
                f"<CurrentURIMetaData>{html.escape(playlist)}</CurrentURIMetaData>"
                "<NextURI></NextURI><NextURIMetaData></NextURIMetaData>",
            )
        elif action == "GetCurrentTransportActions":
            response = envelope(
                action, AV, "<Actions>Set,Stop,Pause,Play,Seek,Next,Previous</Actions>"
            )
        elif action == "GetTransportSettings":
            response = envelope(
                action,
                AV,
                f"<PlayMode>{'SHUFFLE_NOREPEAT' if State.shuffle else 'NORMAL'}</PlayMode>",
            )
        elif action == "SetPlayMode":
            State.shuffle = "SHUFFLE" in self.field(body, "NewPlayMode", "")
            response = envelope(action, AV)
        elif action in ("GetVolume", "GetGroupVolume"):
            namespace = GRC if "Group" in action else RC
            response = envelope(action, namespace, f"<CurrentVolume>{State.volume}</CurrentVolume>")
        elif action in ("GetMute", "GetGroupMute"):
            namespace = GRC if "Group" in action else RC
            response = envelope(
                action, namespace, f"<CurrentMute>{1 if State.muted else 0}</CurrentMute>"
            )
        elif action in ("SetVolume", "SetGroupVolume"):
            State.volume = max(0, min(100, int(self.field(body, "DesiredVolume", "0"))))
            response = envelope(action, GRC if "Group" in action else RC)
        elif action == "SetRelativeGroupVolume":
            adjustment = int(self.field(body, "Adjustment", "0"))
            State.volume = max(0, min(100, State.volume + adjustment))
            response = envelope(action, GRC, f"<NewVolume>{State.volume}</NewVolume>")
        elif action in ("SetMute", "SetGroupMute"):
            State.muted = self.field(body, "DesiredMute", "0") in ("1", "true")
            response = envelope(action, GRC if "Group" in action else RC)
        elif action == "Browse":
            response = self.browse(body)
        elif action == "Play":
            if (State.current_uri == "x-sonosapi-stream:transitioning" and
                    not State.radio_transition_started):
                State.radio_transition_started = True
                State.radio_transition = True
                self.send_xml(fault(), 500)
                return
            State.playing = True
            response = envelope(action, AV)
        elif action in ("Pause", "Stop"):
            State.playing = False
            response = envelope(action, AV)
        elif action == "RemoveAllTracksFromQueue":
            State.queue_cleared = True
            State.loaded_playlist_id = 0
            State.loaded_from_favorite = False
            response = envelope(action, AV)
        elif action == "AddURIToQueue":
            enqueued_uri = self.field(body, "EnqueuedURI")
            playlist_id = saved_playlist_id(enqueued_uri)
            if playlist_id and State.queue_cleared:
                State.loaded_playlist_id = playlist_id
                State.loaded_from_favorite = False
                response = envelope(
                    action,
                    AV,
                    "<FirstTrackNumberEnqueued>1</FirstTrackNumberEnqueued>"
                    "<NumTracksAdded>8</NumTracksAdded><NewQueueLength>8</NewQueueLength>",
                )
            elif (State.queue_cleared and
                  enqueued_uri.startswith("x-rincon-cpcontainer:")):
                State.loaded_playlist_id = 2
                State.loaded_from_favorite = True
                response = envelope(
                    action,
                    AV,
                    "<FirstTrackNumberEnqueued>1</FirstTrackNumberEnqueued>"
                    "<NumTracksAdded>8</NumTracksAdded><NewQueueLength>8</NewQueueLength>",
                )
            else:
                response = envelope(
                    action,
                    AV,
                    "<FirstTrackNumberEnqueued>4</FirstTrackNumberEnqueued>"
                    "<NumTracksAdded>3</NumTracksAdded><NewQueueLength>11</NewQueueLength>",
                )
        elif action == "SetAVTransportURI":
            State.current_uri = self.field(body, "CurrentURI", State.current_uri)
            State.radio_transition_started = False
            response = envelope(action, AV)
        elif action in (
            "Next",
            "Previous",
            "Seek",
            "BecomeCoordinatorOfStandaloneGroup",
        ):
            response = envelope(action, AV)
        else:
            self.send_xml(fault(401, "Invalid Action"), 500)
            return
        self.send_xml(response)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1400)
    parser.add_argument(
        "--scenario",
        choices=[
            "one",
            "normal",
            "multi-room",
            "grouped",
            "long-queue",
            "no-artwork",
            "stereo",
            "home-theater",
            "delay",
            "slow",
            "fault",
            "malformed",
            "coordinator-change",
        ],
        default="grouped",
    )
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    State.scenario = args.scenario
    server = Server((args.host, args.port), Handler)
    server.verbose = args.verbose
    print(f"Miyonos mock Sonos listening on {args.host}:{args.port} ({args.scenario})", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
