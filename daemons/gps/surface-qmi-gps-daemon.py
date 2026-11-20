#!/usr/bin/env python3
import os
import socket
import gi
gi.require_version("Qmi", "1.0")
gi.require_version("Qrtr", "1.0")
from gi.repository import GLib, Gio, Qmi, Qrtr

SOCK_PATH = "/run/surface-gps.sock"
clients = set()
qmi_client = None
gps_running = False

def start_gps():
    global gps_running
    if not qmi_client or gps_running:
        return
    gps_running = True
    inp = Qmi.MessageLocStartInput.new()
    inp.set_session_id(1)
    inp.set_intermediate_report_state(Qmi.LocIntermediateReportState.ENABLE)
    inp.set_minimum_interval_between_position_reports(1000)
    qmi_client.start(inp, 10, None, lambda c, r: None)

def stop_gps():
    global gps_running
    if not qmi_client or not gps_running:
        return
    gps_running = False
    inp = Qmi.MessageLocStopInput.new()
    inp.set_session_id(1)
    qmi_client.stop(inp, 10, None, lambda c, r: None)

def on_nmea(_client, output):
    try:
        line = (output.get_nmea_string().strip() + "\r\n").encode("ascii", errors="ignore")
    except Exception:
        return
    dead = []
    for conn in list(clients):
        try:
            conn.sendall(line)
        except Exception:
            dead.append(conn)
    for conn in dead:
        clients.discard(conn)
        try:
            conn.close()
        except Exception:
            pass
    if not clients:
        stop_gps()

def on_accept(sock, _cond):
    try:
        conn, _ = sock.accept()
        conn.setblocking(False)
        clients.add(conn)
        start_gps()
    except Exception:
        pass
    return True

def setup_qmi():
    def on_reg(client, res):
        global qmi_client
        try:
            client.register_events_finish(res)
        except Exception:
            pass
        qmi_client = client
        if clients:
            start_gps()

    def on_set_nmea(client, res):
        try:
            client.set_nmea_types_finish(res)
        except Exception:
            pass
        reg = Qmi.MessageLocRegisterEventsInput.new()
        reg.set_event_registration_mask(Qmi.LocEventRegistrationFlag.NMEA)
        client.register_events(reg, 10, None, on_reg)

    def on_alloc(dev, res):
        client = dev.allocate_client_finish(res)
        client.connect("nmea", on_nmea)
        inp = Qmi.MessageLocSetNmeaTypesInput.new()
        inp.set_nmea_types(
            Qmi.LocNmeaType.GGA | Qmi.LocNmeaType.RMC | Qmi.LocNmeaType.GSA | Qmi.LocNmeaType.GSV | Qmi.LocNmeaType.VTG
        )
        client.set_nmea_types(inp, 10, None, on_set_nmea)

    def on_open(dev, res):
        dev.open_finish(res)
        dev.allocate_client(Qmi.Service.LOC, Qmi.CID_NONE, 10, None, on_alloc)

    def on_dev(_u, res):
        dev = Qmi.Device.new_from_node_finish(res)
        dev.open(Qmi.DeviceOpenFlags.NONE, 10, None, on_open)

    def on_bus(_u, res):
        bus = Qrtr.Bus.new_finish(res)
        node = bus.peek_node(0)
        if node:
            Qmi.Device.new_from_node(node, None, on_dev)

    Qrtr.Bus.new(3000, None, on_bus)

if os.path.exists(SOCK_PATH):
    os.unlink(SOCK_PATH)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(SOCK_PATH)
os.chmod(SOCK_PATH, 0o666)
srv.listen(5)
srv.setblocking(False)
GLib.io_add_watch(srv.fileno(), GLib.IO_IN, lambda fd, cond: on_accept(srv, cond))
setup_qmi()
GLib.MainLoop().run()
