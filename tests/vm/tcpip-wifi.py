"""VM test: the TCP/IP panel against real NetworkManager and a simulated
WPA2 Wi-Fi network.

Run inside WSL Debian after scripts/build-iso.sh and scripts/build-debs.sh:

    python3 tests/vm/tcpip-wifi.py

It boots the ISO with a serial console (its kernel and initrd, with
console=ttyS0 added), installs build/packages/zacos9_*.deb, and makes
an access point "ZacTest" (WPA2, password "correct horse") with
mac80211_hwsim and hostapd, in a network namespace of its own. Then it
drives zacos9-tcpip with vptr:

  1. Connect via: Wi-Fi; Network: ZacTest; the password; Join.
  2. A name server typed in; the window closed and the change saved.

and checks NetworkManager's side of each. Screenshots: /tmp/vmtcp-*.png.
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(__file__))
from serial import Serial  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
VM = os.path.expanduser("~/.local/share/zacos9-vm-test")
PASSWORD = "correct horse"
fails = 0


def check(what, ok):
    global fails
    print("ok  " if ok else "FAIL", what, flush=True)
    fails += not ok


def monitor(*commands):
    subprocess.run([sys.executable, os.path.join(ROOT, "tests/vm/monitor.py"),
                    f"{VM}/monitor.sock", *commands], check=True, capture_output=True)


def shot(name):
    path = f"/tmp/vmtcp-{name}.png"
    monitor(f"screendump {path} -f png")
    return path


def origin():
    out = subprocess.run([sys.executable, os.path.join(ROOT, "tests/ui/content-origin.py"),
                          shot("where")], capture_output=True, text=True).stdout.split()
    return int(out[0]), int(out[1])


def main():
    iso = max(glob.glob(f"{ROOT}/build/iso/*.iso"), key=os.path.getmtime)
    deb = max(glob.glob(f"{ROOT}/build/packages/zacos9_*.deb"), key=os.path.getmtime)
    os.makedirs(VM, exist_ok=True)
    for f in ("vmlinuz", "initrd.img"):
        subprocess.run(["xorriso", "-osirrox", "on", "-indev", iso, "-extract", f"/live/{f}",
                        f"{VM}/{f}"], check=True, capture_output=True)
    served = tempfile.mkdtemp()
    shutil.copy(deb, f"{served}/zacos9.deb")
    shutil.copy(f"{ROOT}/build/shell/vptr", f"{served}/vptr")
    http = subprocess.Popen([sys.executable, "-m", "http.server", "8765", "--bind", "0.0.0.0",
                             "--directory", served], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    for s in ("serial.sock", "monitor.sock"):
        if os.path.exists(f"{VM}/{s}"):
            os.remove(f"{VM}/{s}")
    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-m", "4096", "-smp", "4",
        "-enable-kvm", "-cpu", "host", "-device", "virtio-vga",
        "-device", "qemu-xhci", "-device", "usb-tablet", "-device", "usb-kbd",
        "-nic", "user,model=virtio-net-pci", "-cdrom", iso,
        "-kernel", f"{VM}/vmlinuz", "-initrd", f"{VM}/initrd.img",
        "-append", "boot=live components quiet hostname=zacos9 username=user "
                   "console=tty0 console=ttyS0,115200",
        "-serial", f"unix:{VM}/serial.sock,server,nowait",
        "-monitor", f"unix:{VM}/monitor.sock,server,nowait", "-display", "none"])
    try:
        run_test(keep="--keep" in sys.argv)
    finally:
        qemu.terminate()
        http.terminate()
    sys.exit(1 if fails else 0)


def run_test(keep):
    con = Serial(f"{VM}/serial.sock")
    con.login()
    print("logged in over the serial console", flush=True)

    def sh(command, timeout=300, quiet=False):
        status, out = con.run(command, timeout)
        if not quiet or status:
            print(f"$ {command}\n{out}" + (f"\n(exit {status})" if status else ""), flush=True)
        return status, out

    sh("export WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/$(id -u); "
       "nmcli() { command nmcli --colors no \"$@\"; }", quiet=True)
    sh("until systemctl is-active -q NetworkManager && nmcli -t -f STATE general | grep -q "
       "'^connected'; do sleep 2; done", timeout=180)
    sh("cd /tmp && wget -q http://10.0.2.2:8765/zacos9.deb http://10.0.2.2:8765/vptr "
       "&& chmod +x vptr && sudo dpkg -i zacos9.deb >/dev/null", timeout=120)
    status, _ = sh("sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive "
                   "apt-get install -y -qq hostapd iw >/dev/null", timeout=900)
    check("hostapd and iw installed", status == 0)

    # The access point: the second simulated radio, in its own namespace.
    with open(f"{VM}/hostapd.conf", "w") as f:
        f.write("interface=wlan1\ndriver=nl80211\nssid=ZacTest\nhw_mode=g\nchannel=6\n"
                f"wpa=2\nwpa_key_mgmt=WPA-PSK\nrsn_pairwise=CCMP\nwpa_passphrase={PASSWORD}\n")
    for line in open(f"{VM}/hostapd.conf"):
        sh(f"echo '{line.rstrip()}' >> /tmp/hostapd.conf", quiet=True)
    sh("sudo modprobe mac80211_hwsim radios=2 && sleep 3 && "
       "sudo ip netns add ap && "
       "sudo iw phy $(cat /sys/class/net/wlan1/phy80211/name) set netns name ap && "
       "sudo ip netns exec ap ip addr add 192.168.77.1/24 dev wlan1 && "
       "sudo ip netns exec ap hostapd -B /tmp/hostapd.conf >/dev/null && "
       "sudo ip netns exec ap /usr/sbin/dnsmasq --interface=wlan1 --bind-interfaces "
       "--dhcp-range=192.168.77.10,192.168.77.50,12h --pid-file=/tmp/dnsmasq-ap.pid")
    _, nets = sh("for i in $(seq 20); do nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi "
                 "list --rescan yes | grep ZacTest && break; sleep 3; done", timeout=120)
    check("NetworkManager sees ZacTest", "ZacTest" in nets)
    sh("nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device")
    if keep:
        # For debugging: leave the VM up; tests/vm/serial.py reaches it.
        print("VM kept running", flush=True)
        con.s.close()  # QEMU's serial socket takes one client at a time
        while True:
            time.sleep(60)

    # A fresh session, so the new menu bar runs; the panel must be opened
    # from it (polkit lets the active local session change the network,
    # not this serial login).
    sh("sudo systemctl restart greetd", quiet=True)
    # The automatic login is once per boot: log in at the login screen,
    # typing on the VM's keyboard.
    time.sleep(8)
    for word in ("user", "live"):
        monitor(*[f"sendkey {ch}" for ch in word], "sendkey ret")
        time.sleep(2)
    deadline = time.time() + 120
    time.sleep(10)
    while time.time() < deadline:
        up = subprocess.run([sys.executable, os.path.join(ROOT, "tests/vm/desktop-up.py"),
                             shot("0-desktop")]).returncode == 0
        if up:
            break
        time.sleep(3)
    time.sleep(5)
    # Logo menu (a quick click leaves it open), Control Panels, TCP/IP:
    # rows of 16 px below the menu bar's line at y=19; About This
    # Computer…, a separator, Classic, Control Panels; TCP/IP is the
    # seventh panel in the submenu.
    sh("/tmp/vptr home move 22 9 click wait 700 move 18 57 wait 700", quiet=True)
    shot("0-logo-menu")
    sh("/tmp/vptr move 170 0 wait 300 move 0 96 wait 500", quiet=True)
    shot("0-control-panels")
    sh("/tmp/vptr click wait 6000", quiet=True)
    shot("1-ethernet")

    # 1. Join ZacTest from the panel.
    def click(x, y, then=""):
        ox, oy = origin()
        sh(f"/tmp/vptr home move {ox + x} {oy + y} click wait 800 {then}", quiet=True)

    click(300, 24)                                   # Connect via (sticky)
    shot("2-via-menu")
    sh("/tmp/vptr move 0 16 click wait 2500", quiet=True)  # Wi-Fi
    shot("3-wifi")
    click(300, 71)                                   # Network
    shot("4-network-menu")
    sh("/tmp/vptr move 0 16 click wait 2000", quiet=True)  # ZacTest
    sh(f"/tmp/vptr type '{PASSWORD}' wait 500", quiet=True)
    shot("5-password")
    sh("/tmp/vptr key Return", quiet=True)
    sh("for i in $(seq 30); do nmcli -t -f DEVICE,STATE device | grep -q '^wlan0:connected$' "
       "&& break; sleep 1; done", timeout=60)
    time.sleep(5)
    shot("6-joined")
    _, dev = sh("nmcli -t -f DEVICE,STATE,CONNECTION device | grep ^wlan0")
    check("wlan0 joined ZacTest", dev.strip() == "wlan0:connected:ZacTest")
    _, addr = sh("ip -4 -o addr show wlan0")
    check("wlan0 got an address from the access point", "192.168.77." in addr)
    _, psk = sh("sudo nmcli -s -g 802-11-wireless-security.psk connection show ZacTest",
                quiet=True)
    check("NetworkManager kept the password with the profile", psk.strip() == PASSWORD)

    # 2. A name server, saved on close.
    click(200, 240)                                  # Name server addr.
    sh("/tmp/vptr type 9.9.9.9 wait 300 cmd w wait 2000", quiet=True)
    shot("7-save-alert")
    sh("/tmp/vptr key Return wait 4000", quiet=True)  # Save
    _, dns = sh("nmcli -g ipv4.dns connection show ZacTest")
    check("the name server was saved to the profile", dns.strip() == "9.9.9.9")
    # The interface comes up again with it (DHCP and all): give it time.
    _, live = sh("for i in $(seq 30); do nmcli -t -f IP4.DNS device show wlan0 | grep 9.9.9.9 "
                 "&& break; sleep 1; done", timeout=60)
    check("and is in use", "9.9.9.9" in live)


if __name__ == "__main__":
    main()
