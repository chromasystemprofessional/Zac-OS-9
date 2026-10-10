#!/usr/bin/env python3
"""Check the opt-in bridge's transport and authentication boundaries."""

import importlib.machinery
import importlib.util
import os
from pathlib import Path
import sys
import socket
import threading
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


HELPER = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[2] / "session/zacos9-rdp-helper")
loader = importlib.machinery.SourceFileLoader("rdp_helper", str(HELPER))
spec = importlib.util.spec_from_loader(loader.name, loader)
helper = importlib.util.module_from_spec(spec)
loader.exec_module(helper)
rfb_loader = importlib.machinery.SourceFileLoader(
    "rdp_rfb", str(HELPER.with_name("zacos9-rdp-rfb")))
rfb_spec = importlib.util.spec_from_loader(rfb_loader.name, rfb_loader)
rfb = importlib.util.module_from_spec(rfb_spec)
rfb_loader.exec_module(rfb)


class RdpConfig(unittest.TestCase):
    def test_frontend_requires_tls_and_account_authentication(self):
        config = helper.xrdp_config()
        self.assertIn("security_layer=tls\n", config)
        self.assertIn("ssl_protocols=TLSv1.2,TLSv1.3\n", config)
        self.assertIn("require_credentials=true\n", config)
        self.assertIn("pamusername=same\npampassword=same\n", config)
        self.assertIn("port=unix://./run/zacos9-rdp/transport/rdp.sock\n", config)
        self.assertIn("ip=127.0.0.1\nport=5900\n", config)
        self.assertNotIn("vnc-any", config)
        self.assertEqual(config.count("username=ask\n"), 1)
        self.assertIn("allow_channels=false\n", config)

    def test_gateway_authorizes_only_the_sharing_group(self):
        config = helper.auth_config("zacos9-rdp-1001")
        self.assertIn("AllowRootLogin=false\n", config)
        self.assertIn("TerminalServerUsers=zacos9-rdp-1001\n", config)
        self.assertIn("AlwaysGroupCheck=true\n", config)
        self.assertIn("MaxLoginRetry=3\n", config)
        self.assertIn("AllowAlternateShell=false\n", config)
        self.assertIn("LogFile=/run/zacos9-rdp/auth.log\n", config)
        self.assertIn("LogFile=/run/zacos9-rdp/transport/xrdp.log\n",
                      helper.xrdp_config())

    def test_backend_has_a_private_network_and_logout_lifecycle(self):
        units = helper.service_units(1001, "192.168.2.110")
        bridge = units["zacos9-rdp-bridge"]
        listener = units["zacos9-rdp-listener"]
        self.assertIn("PrivateNetwork=true\n", bridge)
        self.assertIn("BindsTo=user@1001.service\n", bridge)
        self.assertIn("/run/user/1001/zacos9-rdp/vnc.sock", bridge)
        self.assertIn("BindsTo=zacos9-rdp-bridge.service\n", listener)
        self.assertIn("bind=192.168.2.110,", listener)
        self.assertIn("User=xrdp\n", listener)
        self.assertNotIn("WantedBy", "".join(units.values()))

    def test_rejects_non_lan_addresses_and_unit_injection(self):
        for address in ("0.0.0.0", "8.8.8.8", "224.0.0.1", "169.254.1.2",
                        "192.0.2.1", "::1", "192.168.2.110\nExecStart=/bin/sh"):
            with self.subTest(address=address), self.assertRaises(ValueError):
                helper.validate_address(address)

    def test_validates_that_address_belongs_to_this_machine(self):
        with patch.object(helper.socket, "socket") as socket:
            self.assertEqual(helper.validate_address("192.168.2.110"), "192.168.2.110")
            socket.return_value.__enter__.return_value.bind.assert_called_once_with(
                ("192.168.2.110", 0))
        with patch.object(helper.socket, "socket") as socket:
            socket.return_value.__enter__.return_value.bind.side_effect = OSError(
                "Cannot assign requested address")
            with self.assertRaises(OSError):
                helper.validate_address("192.168.2.120")

    def test_capture_uses_only_private_unix_sockets(self):
        launcher = HELPER.with_name("zacos9-screen-sharing").read_text()
        self.assertIn("--property=UMask=0077", launcher)
        self.assertIn("--property=RuntimeDirectoryMode=0700", launcher)
        self.assertIn("wayvnc -C /dev/null -R -u", launcher)
        self.assertIn('systemctl --user stop "$unit"', launcher)

    def test_stop_handles_an_already_stopped_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            command = Path(directory) / "systemctl"
            command.write_text("#!/bin/sh\n"
                "if [ \"$1\" = --user ]; then echo not-found; else echo inactive; fi\n")
            command.chmod(0o755)
            result = subprocess.run((
                "/bin/sh", str(HELPER.with_name("zacos9-screen-sharing")), "stop"),
                env={**os.environ, "PATH": directory}, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("RDP sharing stopped.", result.stdout)

    def test_stop_does_not_hide_session_bus_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            command = Path(directory) / "systemctl"
            command.write_text("#!/bin/sh\necho 'Cannot connect to session bus' >&2\nexit 1\n")
            command.chmod(0o755)
            result = subprocess.run((
                "/bin/sh", str(HELPER.with_name("zacos9-screen-sharing")), "stop"),
                env={**os.environ, "PATH": directory}, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Cannot connect", result.stderr)
            self.assertNotIn("RDP sharing stopped.", result.stdout)

    def test_polkit_requires_administrator_authentication(self):
        policy = ET.parse(HELPER.with_name("org.zacos9.rdp.policy"))
        action = policy.getroot().find("action")
        self.assertEqual(action.find("defaults/allow_active").text, "auth_admin_keep")
        self.assertEqual(action.find("defaults/allow_any").text, "auth_admin")
        self.assertEqual(action.find("annotate").text, helper.HELPER)

    def test_rfb_handshake_adapts_33_to_38(self):
        front, client = socket.socketpair()
        back, server = socket.socketpair()
        for peer in (front, client, back, server):
            self.addCleanup(peer.close)
            peer.settimeout(3)
        errors = []

        def negotiate():
            try:
                rfb.handshake(front, back)
            except (OSError, ValueError, EOFError) as error:
                errors.append(error)

        thread = threading.Thread(target=negotiate, daemon=True)
        thread.start()
        server.sendall(b"RFB 003.008\n")
        self.assertEqual(rfb.read_exact(client, 12), b"RFB 003.003\n")
        client.sendall(b"RFB 003.003\n")
        self.assertEqual(rfb.read_exact(server, 12), b"RFB 003.008\n")
        server.sendall(b"\x01\x01")
        self.assertEqual(rfb.read_exact(server, 1), b"\x01")
        server.sendall(bytes(4))
        self.assertEqual(rfb.read_exact(client, 4), b"\x00\x00\x00\x01")
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(rfb.Server.address_family, socket.AF_INET6)

    def test_relay_preserves_buffered_data_before_half_close(self):
        front, client = socket.socketpair()
        back, server = socket.socketpair()
        for peer in (front, client, back, server):
            self.addCleanup(peer.close)
            peer.settimeout(3)
        errors = []

        def transfer():
            try:
                rfb.relay(front, back)
            except (OSError, EOFError) as error:
                errors.append(error)

        thread = threading.Thread(target=transfer, daemon=True)
        thread.start()
        payload = b"RDP framebuffer bytes" * 10000
        client.sendall(payload)
        client.shutdown(socket.SHUT_WR)
        self.assertEqual(rfb.read_exact(server, len(payload)), payload)
        self.assertEqual(server.recv(1), b"")
        server.sendall(b"final response")
        server.shutdown(socket.SHUT_WR)
        self.assertEqual(rfb.read_exact(client, 14), b"final response")
        self.assertEqual(client.recv(1), b"")
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
