"""Check the nmcli calls tests/ui/tcpip.sh made (from tests/ui/fake-nmcli's log).

usage: tcpip-check.py [LOG]
"""
import json
import sys

log = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fake-nmcli.log"
calls = [json.loads(line) for line in open(log)]
fails = 0


def check(what, ok):
    global fails
    print("ok  " if ok else "FAIL", what)
    fails += not ok


def find(*words):
    return [c for c in calls if all(w in c["args"] for w in words)]


adds = find("connection", "add", "type", "wifi", "Neighbor")
check("a profile for Neighbor, WPA personal",
      any(c["args"][c["args"].index("wifi-sec.key-mgmt") + 1] == "wpa-psk" for c in adds
          if "wifi-sec.key-mgmt" in c["args"]))
ups = find("connection", "up", "Neighbor", "passwd-file")
check("joined through passwd-file on standard input",
      any(c["stdin"] == "802-11-wireless-security.psk:correct horse\n" for c in ups))
check("the password never went on a command line",
      not any("correct horse" in a for c in calls for a in c["args"]))
check("no polkit prompt to feed it to (no --ask)", not find("--ask"))

mods = find("connection", "modify", "u-Neighbor")
args = mods[-1]["args"] if mods else []


def setting(k):
    return args[args.index(k) + 1] if k in args else None


check("saved to the joined network's profile", bool(mods))
check("ipv4.method manual", setting("ipv4.method") == "manual")
check("ipv4.addresses 192.168.1.50/24", setting("ipv4.addresses") == "192.168.1.50/24")
check("ipv4.gateway from the server's router", setting("ipv4.gateway") == "192.168.1.1")
check("brought up with the new settings", bool(find("connection", "up", "u-Neighbor")))
sys.exit(1 if fails else 0)
