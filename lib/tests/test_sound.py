"""Playback lifecycle tests with mock executables, never audio hardware."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import unittest
import uuid

DRIVER = str(Path(sys.argv[1]).resolve())
DATA = str(Path(sys.argv[2]).resolve())
sys.argv[1:] = []


class PlaybackTests(unittest.TestCase):
    def setUp(self):
        self.root = Path.cwd() / (".sound-test-" + uuid.uuid4().hex)
        (self.root / "zacos9/sounds").mkdir(parents=True)
        self.log = self.root / "log"
        self.conf = self.root / "zacos9/desktop.conf"
        self.settings = {"sound-theme": "imported", "interface-volume": "5",
                         "sound.window-drag": "drag.wav",
                         "sound.window-collapse": "collapse.wav",
                         "sound.window-expand": "expand.wav",
                         "sound.window-drag-end": "end.wav"}
        for name in ("drag", "collapse", "expand", "end"):
            (self.root / f"zacos9/sounds/{name}.wav").write_bytes(b"mock")
        self.configure()
        self.player("pw-play")
        env = dict(os.environ, PATH=str(self.root), SOUND_TEST_LOG=str(self.log),
                   XDG_CONFIG_HOME=str(self.root), XDG_CACHE_HOME=str(self.root),
                   XDG_RUNTIME_DIR=str(self.root), ZACOS9_DATA=DATA)
        self.driver = subprocess.Popen([DRIVER], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, text=True, env=env)

    def player(self, name, duration=0.12, descendants=False):
        script = f"""#!{sys.executable}
import json, os, signal, sys, time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
with open(os.environ["SOUND_TEST_LOG"], "a") as f:
    f.write(json.dumps({{"pid": os.getpid(), "parent": os.getppid(),
                        "backend": os.path.basename(sys.argv[0]), "args": sys.argv[1:]}}) + "\\n")
"""
        if descendants:
            script += """if os.fork() == 0:
    while True:
        time.sleep(1)
"""
        script += f"time.sleep({duration})\n"
        path = self.root / name
        path.write_text(script)
        path.chmod(0o700)

    def configure(self, **values):
        self.settings.update(values)
        new = self.conf.with_suffix(".new")
        new.write_text("[General]\n" + "".join(f"{k}={v}\n"
                                              for k, v in self.settings.items()))
        new.replace(self.conf)

    def records(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def wait_for(self, predicate):
        end = time.monotonic() + 4
        while time.monotonic() < end:
            if predicate():
                return
            time.sleep(0.02)
        self.fail("Timed out waiting for mock playback/cleanup")

    def command(self, text):
        before = time.monotonic()
        self.driver.stdin.write(text + "\n")
        self.driver.stdin.flush()
        self.assertEqual(self.driver.stdout.readline(), "ok\n")
        self.assertLess(time.monotonic() - before, 0.5)

    def dead(self, pid):
        try:
            status = Path(f"/proc/{pid}/stat").read_text()
            return status.split(") ", 1)[1].startswith("Z")
        except (FileNotFoundError, ProcessLookupError):
            return True

    def cleanup_complete(self, records):
        self.wait_for(lambda: all(self.dead(r["pid"]) and
                                 (r["parent"] == 1 or self.dead(r["parent"]))
                                  for r in records))

    def tearDown(self):
        if self.driver.poll() is None:
            self.driver.stdin.write("quit\n")
            self.driver.stdin.flush()
        self.driver.wait(timeout=4)
        self.driver.stdin.close()
        self.driver.stdout.close()
        self.cleanup_complete(self.records())
        shutil.rmtree(self.root)

    def test_repeat_stop_and_restart(self):
        self.command("reaper")  # The compositor also owns SIGCHLD.
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) >= 3)
        records = self.records()
        self.assertTrue(all(r["args"][:2] == ["--volume", "0.714"] for r in records))
        self.command("stop")
        self.command("stop")
        self.cleanup_complete(records)
        self.wait_for(lambda: all(not Path(f"/proc/{r['parent']}").exists()
                                  for r in records))
        count = len(self.records())
        time.sleep(0.3)
        self.assertEqual(len(self.records()), count)
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) > count)
        self.command("start window-expand")
        self.wait_for(lambda: any(r["args"][-1].endswith("expand.wav")
                                  for r in self.records()))

    def test_stop_long_backend_and_caller_exit(self):
        self.player("pw-play", 30, descendants=True)
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) >= 1)
        records = self.records()
        backend = records[0]["pid"]
        self.wait_for(lambda: bool(Path(f"/proc/{backend}/task/{backend}/children").read_text().strip()))
        child = int(Path(f"/proc/{backend}/task/{backend}/children").read_text().strip())
        self.command("stop")
        self.cleanup_complete(records)
        self.wait_for(lambda: self.dead(child))
        count = len(records)
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) > count)
        records = self.records()
        self.driver.stdin.write("quit\n")
        self.driver.stdin.flush()
        self.driver.wait(timeout=2)
        self.cleanup_complete(records)

    def test_silent_initial_settings(self):
        for values in ({"sound-theme": "none"}, {"sound-theme": "imported", "interface-volume": "0"},
                       {"interface-volume": "5", "sound.window-drag": "missing.wav"},
                       {"sound.window-drag": "../drag.wav"}):
            self.configure(**values)
            self.command("start window-drag")
            time.sleep(0.2)
            self.assertEqual(self.records(), [])
        self.command("start unknown")
        self.command("start window-close")  # Missing theme mapping.
        time.sleep(0.2)
        self.assertEqual(self.records(), [])

    def test_live_mute_theme_and_volume(self):
        self.player("pw-play", 30)
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) == 1)
        self.configure(**{"interface-volume": "0"})
        self.wait_for(lambda: self.dead(self.records()[0]["pid"]))
        time.sleep(0.2)
        self.assertEqual(len(self.records()), 1)
        self.configure(**{"interface-volume": "2"})
        self.wait_for(lambda: len(self.records()) == 2)
        self.assertEqual(self.records()[-1]["args"][:2], ["--volume", "0.286"])
        self.configure(**{"sound-theme": "none"})
        self.wait_for(lambda: self.dead(self.records()[-1]["pid"]))
        time.sleep(0.2)
        self.assertEqual(len(self.records()), 2)
        self.configure(**{"sound-theme": "imported", "sound.window-drag": "missing.wav"})
        time.sleep(0.2)
        self.assertEqual(len(self.records()), 2)
        self.configure(**{"sound.window-drag": "drag.wav"})
        self.wait_for(lambda: len(self.records()) == 3)

    def test_routes_and_original_assets(self):
        for event, filename in (("window-collapse", "collapse.wav"),
                                ("window-expand", "expand.wav"),
                                ("window-drag-end", "end.wav")):
            count = len(self.records())
            self.command("event " + event)
            self.wait_for(lambda: len(self.records()) > count)
            self.assertTrue(self.records()[-1]["args"][-1].endswith(filename))
            self.wait_for(lambda: self.dead(self.records()[-1]["pid"]))
        self.configure(**{"sound-theme": "original"})
        for event, filename in (("window-collapse", "pluck.wav"),
                                ("window-expand", "chirp.wav"),
                                ("window-drag", "droplet.wav"),
                                ("window-drag-end", "woodblock.wav")):
            count = len(self.records())
            self.command("event " + event)
            self.wait_for(lambda: len(self.records()) > count)
            self.assertEqual(self.records()[-1]["args"][-1], f"{DATA}/sounds/{filename}")
            self.wait_for(lambda: self.dead(self.records()[-1]["pid"]))

    def test_contention_retry_and_preview(self):
        import fcntl
        with (self.root / "zacos9-interface-sound.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            self.command("start window-drag")
            time.sleep(0.3)
            self.assertEqual(self.records(), [])
            # Preview deliberately bypasses the interface lock.
            self.command(f"preview {self.root}/zacos9/sounds/expand.wav")
            self.wait_for(lambda: len(self.records()) == 1)
            self.wait_for(lambda: self.dead(self.records()[0]["pid"]))
        self.wait_for(lambda: len(self.records()) >= 3)

    def test_interface_events_do_not_overlap_loop(self):
        self.player("pw-play", 30)
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) == 1)
        self.command("event window-collapse")
        time.sleep(0.2)
        self.assertEqual(len(self.records()), 1)
        self.command("stop")
        self.cleanup_complete(self.records())
        self.player("pw-play")
        self.command("event window-collapse")
        self.wait_for(lambda: len(self.records()) == 2)
        self.assertTrue(self.records()[-1]["args"][-1].endswith("collapse.wav"))

    def test_immediate_stop_then_drag_end(self):
        self.player("pw-play", 30)
        self.command("reaper")
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) == 1)
        drag = self.records()[0]
        self.command("event window-collapse")  # Update one-shot throttle.
        self.command("stop")
        self.player("pw-play")
        self.command("event window-drag-end")
        self.wait_for(lambda: len(self.records()) == 2)
        self.assertTrue(self.records()[-1]["args"][-1].endswith("end.wav"))
        self.assertTrue(self.dead(drag["pid"]))

    def test_drag_end_contention_is_bounded(self):
        import fcntl
        with (self.root / "zacos9-interface-sound.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            self.command("event window-drag-end")
            time.sleep(0.5)
            self.assertEqual(self.records(), [])
        time.sleep(0.2)
        self.assertEqual(self.records(), [])

    def test_pending_one_shot_does_not_delay_stop_eof(self):
        self.player("pw-play", 30)
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) == 1)
        drag = self.records()[0]
        # This detached one-shot waits for the lock before exec; it must not
        # retain a copy of the loop control writer during that wait.
        self.command("event window-drag-end")
        self.player("pw-play")
        before = time.monotonic()
        self.command("stop")
        self.wait_for(lambda: self.dead(drag["pid"]))
        self.assertLess(time.monotonic() - before, 0.2)
        self.wait_for(lambda: len(self.records()) == 2)
        self.assertTrue(self.records()[-1]["args"][-1].endswith("end.wav"))

    def test_backend_fallback(self):
        (self.root / "pw-play").unlink()
        self.player("paplay")
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) >= 2)
        self.assertEqual(self.records()[0]["backend"], "paplay")
        self.assertEqual(self.records()[0]["args"][0], "--volume=46811")
        self.command("stop")
        self.cleanup_complete(self.records())
        (self.root / "paplay").unlink()
        self.player("aplay")
        count = len(self.records())
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) > count)
        self.assertEqual(self.records()[-1]["backend"], "aplay")
        self.assertEqual(self.records()[-1]["args"][0], "-q")

    def test_failed_backend_does_not_repeat_forever(self):
        player = self.root / "pw-play"
        player.write_text(player.read_text() + "\nsys.exit(1)\n")
        self.command("start window-drag")
        self.wait_for(lambda: len(self.records()) == 1)
        self.cleanup_complete(self.records())
        time.sleep(0.3)
        self.assertEqual(len(self.records()), 1)
        self.command("stop")


if __name__ == "__main__":
    unittest.main()
