"""Run shell commands in the test VM over its serial console.

A QEMU serial port on a Unix socket (`-serial unix:PATH,server,nowait`)
with a getty on it (`console=ttyS0` on the kernel command line). Logs in
as the live user (user / live) if it finds a login prompt.

As a module: Serial(path), .login(), .run("command") -> (status, output).
As a program: serial.py SOCKET COMMAND  (prints the output, exits with
the command's status).
"""
import re
import socket
import sys
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]|\r")


class Serial:
    def __init__(self, path, timeout=600):
        deadline = time.time() + timeout
        while True:
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(1)
        self.s.settimeout(0.5)
        self.buf = ""

    def _read(self):
        try:
            data = self.s.recv(65536)
        except socket.timeout:
            return
        self.buf += ANSI.sub("", data.decode(errors="replace"))

    def expect(self, pattern, timeout=60):
        """Wait for a regex in the output. Returns the match and the text
        before it, and consumes both."""
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            m = rx.search(self.buf)
            if m:
                before = self.buf[:m.start()]
                self.buf = self.buf[m.end():]
                return m, before
            self._read()
        raise TimeoutError(f"no {pattern!r} in {self.buf[-400:]!r}")

    def send(self, text):
        self.s.sendall(text.encode())

    def login(self, user="user", password="live", timeout=600):
        """Get a shell, logging in if a login prompt shows up (or using the
        quiet shell an earlier connection left)."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.buf = ""
            self.send("\r")
            try:
                m, _ = self.expect(r"login: |\$ $|# $", timeout=3)
            except TimeoutError:
                # A quiet shell from before shows no prompt: ask it.
                self.send('echo __RE""ADY__\r')
                try:
                    self.expect(r"__READY__", timeout=3)
                except TimeoutError:
                    continue
                m = None
            if m and m.group(0) == "login: ":
                self.send(user + "\r")
                self.expect(r"[Pp]assword: ", timeout=20)
                self.send(password + "\r")
                self.expect(r"\$ $", timeout=60)
            # A quiet shell: no echo, no prompt, no pager, no colours.
            self.send("stty -echo; PS1=''; export PAGER=cat SYSTEMD_PAGER=cat NO_COLOR=1; "
                      "bind 'set enable-bracketed-paste off' 2>/dev/null\r")
            time.sleep(1)
            self._read()
            self.buf = ""
            return
        raise TimeoutError("no login prompt")

    def run(self, command, timeout=300):
        """Run a command; return (status, output)."""
        self.buf = ""
        # The marker is split in the command so only its output matches.
        self.send(f"{command}; echo \"__E\"\"ND__$?\"\r")
        m, before = self.expect(r"__END__(\d+)", timeout)
        return int(m.group(1)), before.strip("\n")


if __name__ == "__main__":
    con = Serial(sys.argv[1])
    con.login()
    status, out = con.run(sys.argv[2])
    print(out)
    sys.exit(status)
