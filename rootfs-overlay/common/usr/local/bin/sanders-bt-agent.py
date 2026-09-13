#!/usr/bin/env python3
# sanders-bt-agent.py — agente de pareamento headless para o servidor
# (NoInputNoOutput / Just Works, auto-aceita). Postura de seguranca:
# aceita pareamento quando iniciado pelo par (pairable on), mas NAO
# fica discoverable por padrao — use `sanders-bluetooth.sh on` ou
# bluetoothctl discoverable on quando quiser ser visivel. Rede local
# confiavel. — BF, 2026-09-10 (plano BLUETOOTH_SUBSYSTEM_PLAN.md Fase 4)
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

AGENT_PATH = "/sanders/bt/agent"
AGENT_IFACE = "org.bluez.Agent1"
AGENT_MANAGER = "org.bluez.AgentManager1"
mainloop = None

class SandersAgent(dbus.service.Object):
    @dbus.service.method(AGENT_IFACE, in_signature="", out_signature="")
    def Release(self):
        print("agent: Release", flush=True)

    @dbus.service.method(AGENT_IFACE, in_signature="os", out_signature="")
    def AuthorizeService(self, device, uuid):
        print(f"agent: AuthorizeService {device} {uuid} -> aceito", flush=True)

    @dbus.service.method(AGENT_IFACE, in_signature="o", out_signature="s")
    def RequestPinCode(self, device):
        print(f"agent: RequestPinCode {device} -> 0000", flush=True)
        return "0000"

    @dbus.service.method(AGENT_IFACE, in_signature="o", out_signature="u")
    def RequestPasskey(self, device):
        print(f"agent: RequestPasskey {device} -> 0", flush=True)
        return dbus.UInt32(0)

    @dbus.service.method(AGENT_IFACE, in_signature="ouq", out_signature="")
    def DisplayPasskey(self, device, passkey, entered):
        pass

    @dbus.service.method(AGENT_IFACE, in_signature="ou", out_signature="")
    def RequestConfirmation(self, device, passkey):
        print(f"agent: RequestConfirmation {device} {passkey:06d} -> aceito", flush=True)

    @dbus.service.method(AGENT_IFACE, in_signature="o", out_signature="")
    def RequestAuthorization(self, device):
        print(f"agent: RequestAuthorization {device} -> aceito", flush=True)

    @dbus.service.method(AGENT_IFACE, in_signature="", out_signature="")
    def Cancel(self):
        print("agent: Cancel", flush=True)

def main():
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    mgr = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"), AGENT_MANAGER)
    SandersAgent(bus, AGENT_PATH)
    mgr.RegisterAgent(AGENT_PATH, "NoInputNoOutput")
    mgr.RequestDefaultAgent(AGENT_PATH)
    print("sanders-bt-agent: registrado como agente default (NoInputNoOutput)", flush=True)
    global mainloop
    mainloop = GLib.MainLoop()
    mainloop.run()

if __name__ == "__main__":
    main()
