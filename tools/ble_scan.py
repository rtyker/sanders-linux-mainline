#!/usr/bin/env python3
# BLE beacon observer (BlueZ D-Bus) - Phase 4 UC1 - BF 2026-09-10
# Subscribes to ObjectManager.InterfacesAdded (new devices found by discovery)
# and Device1.PropertiesChanged (updates: RSSI/ServiceData) for a --secs window
# and decodes BTHome v2 payloads (UUID 0xFCD2) if present.
# Usage: python3 ble_scan.py [--secs 30] [--max 200]
import time
import argparse

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

# BTHome v2 measurement ids -> (name, size, factor)
BTHOME_TYPES = {
    0x01: ("bat", 1, 1.0),
    0x02: ("temp", 2, 0.01),
    0x03: ("hum", 2, 0.01),
    0x12: ("batt_volt", 2, 0.001),
    0x3A: ("moisture", 1, 1.0),
}


def decode_bthome(data: bytes):
    """BTHome v2: first byte 0x40 (flags: v2, no factors), then (id, value) pairs."""
    if not data or (data[0] & 0xF0) != 0x40:
        return None
    out, i = {}, 1
    try:
        while i < len(data):
            mid = data[i]
            i += 1
            spec = BTHOME_TYPES.get(mid)
            if spec is None:
                return None  # unknown measurement id -> not a payload we can trust
            name, n, fact = spec
            raw = data[i:i + n]
            i += n
            val = int.from_bytes(raw, "little", signed=(mid == 0x02))
            out[name] = val * fact
    except Exception:
        return None
    return out or None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=int, default=30)
    ap.add_argument("--max", type=int, default=200)
    args = ap.parse_args()

    bus = dbus.SystemBus(mainloop=DBusGMainLoop())
    loop = GLib.MainLoop()
    seen = set()
    n_signals = [0]

    def handle(mac, props):
        name = str(props.get("Name", props.get("Alias", "")))
        rssi = props.get("RSSI", None)
        dec = ""
        sd = props.get("ServiceData", None)
        if sd:
            for uuid, blob in sd.items():
                try:
                    u = int(str(uuid).split("-")[0], 16)
                    b = bytes(blob)
                except Exception:
                    continue
                if u == 0xFCD2:  # BTHome
                    d = decode_bthome(b)
                    dec += f" BTHome:{d}" if d else f" BTHome?{b.hex()}"
                else:
                    dec += f" svc{hex(u)}:{len(b)}B"
        key = (mac, name)
        if key in seen:
            return
        seen.add(key)
        ts = time.strftime("%H:%M:%S")
        print(f"{ts} {mac:17} {name[:20]:20} {str(rssi):5} {dec}", flush=True)
        if len(seen) >= args.max:
            loop.quit()

    def on_ifadd(obj_path, interfaces):
        n_signals[0] += 1
        if "org.bluez.Device1" in interfaces:
            props = interfaces["org.bluez.Device1"]
            handle(str(obj_path).rsplit("/", 1)[-1], props)

    def on_props(iface, props, inv, path=None):
        n_signals[0] += 1
        if iface == "org.bluez.Device1":
            handle(str(path).rsplit("/", 1)[-1], props)

    # discovery stream: new objects appear under /org/bluez/hciX
    bus.add_signal_receiver(
        on_ifadd,
        dbus_interface="org.freedesktop.DBus.ObjectManager",
        signal_name="InterfacesAdded",
    )
    bus.add_signal_receiver(
        on_props,
        dbus_interface=dbus.PROPERTIES_IFACE,
        signal_name="PropertiesChanged",
        path_keyword="path",
    )

    ad = dbus.Interface(
        bus.get_object("org.bluez", "/org/bluez/hci0"), "org.bluez.Adapter1"
    )
    ad.StartDiscovery()
    print(f"scanning for {args.secs}s...", flush=True)

    # hard deadline: loop must quit even if zero signals arrive
    GLib.timeout_add_seconds(args.secs, lambda: loop.quit())
    try:
        loop.run()
    finally:
        try:
            ad.StopDiscovery()
        except Exception:
            pass
    print(f"--- {len(seen)} unique devices ({n_signals[0]} signals)", flush=True)


if __name__ == "__main__":
    main()
