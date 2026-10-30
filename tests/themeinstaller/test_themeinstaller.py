import os, subprocess, sys, tempfile

binary = sys.argv[1]
src = tempfile.mkdtemp()
with open(os.path.join(src, "a.png"), "wb") as f:
    f.write(b"png")

config = tempfile.mkdtemp()
env = dict(os.environ)
env["XDG_CONFIG_HOME"] = config
env["QT_QPA_PLATFORM"] = "offscreen"

r = subprocess.run([binary, "--source", src, "--name", "Test"],
                   env=env, capture_output=True, text=True, timeout=60)
assert r.returncode == 0, r.stderr
out = os.path.join(config, "zacos9", "themes", "Test")
assert os.path.isfile(os.path.join(out, "theme.conf")), "theme.conf written"
assert os.path.isfile(os.path.join(out, "icons", "a.png")), "asset copied"
print("OK")
