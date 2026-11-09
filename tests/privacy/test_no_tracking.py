#!/usr/bin/env python3
"""ZacOS 9 does not track its users (PRIVACY.md).

Fails when a known tracking or analytics endpoint appears in shipped source,
or when the code that talks to the network reaches a host the privacy policy
does not list. Adding a network feature is allowed; adding one silently is
not.

    test_no_tracking.py SOURCE_ROOT
"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else
            Path(__file__).resolve().parents[2]).resolve()

# What the package runs. Developer tools, documentation, tests and the
# Apple reference material are not part of the system.
SOURCE_DIRS = ("compositor", "lib", "network", "shell", "system", "session",
               "boot", "hardware", "share", "po")
SUFFIXES = {".c", ".h", ".cpp", ".cc", ".hpp", ".py", ".sh", ".desktop",
            ".service", ".timer", ".conf", ".rules", ".policy"}
SKIP_DIRS = {"build", ".git", "macos", "uploads", "upload assets",
             "emulation", "tests"}

# Endpoints and SDK markers used to profile people. Domains, not generic
# words: audio code legitimately talks about amplitude.
TRACKERS = {
    "google-analytics.com": "Google Analytics",
    "googletagmanager.com": "Google Tag Manager",
    "doubleclick.net": "Google advertising",
    "googlesyndication.com": "Google advertising",
    "googleadservices.com": "Google advertising",
    "connect.facebook.net": "Facebook tracking",
    "facebook.com/tr": "Facebook tracking",
    "analytics.tiktok.com": "TikTok analytics",
    "ads.linkedin.com": "LinkedIn advertising",
    "snap.licdn.com": "LinkedIn advertising",
    "sentry.io": "Sentry error reporting",
    "ingest.sentry.io": "Sentry error reporting",
    "mixpanel.com": "Mixpanel analytics",
    "api.amplitude.com": "Amplitude analytics",
    "segment.io": "Segment analytics",
    "api.segment.io": "Segment analytics",
    "posthog.com": "PostHog analytics",
    "matomo.cloud": "Matomo analytics",
    "plausible.io": "Plausible analytics",
    "umami.is": "Umami analytics",
    "datadoghq.com": "Datadog monitoring",
    "newrelic.com": "New Relic monitoring",
    "bugsnag.com": "Bugsnag error reporting",
    "rollbar.com": "Rollbar error reporting",
    "hotjar.com": "Hotjar analytics",
    "fullstory.com": "FullStory analytics",
    "onesignal.com": "OneSignal push tracking",
    "app-measurement.com": "Firebase analytics",
    "firebaseio.com": "Firebase",
    "crashlytics.com": "Crashlytics",
    "braze.com": "Braze marketing",
    "optimizely.com": "Optimizely profiling",
}

# The code that makes outbound requests: Software Update, Software and
# Bloodhound's web search. Hosts here must be in PRIVACY.md and on this list.
NETWORK_FILES = (
    "shell/update/updatecheck.cpp",
    "shell/update/update.cpp",
    "shell/store/storeclient.cpp",
    "shell/store/store.cpp",
    "shell/finder/finder.cpp",
)
ALLOWED_HOSTS = {
    "api.github.com": "release metadata for Software Update and Software",
    "github.com": "release package downloads",
    "dl.flathub.org": "the Flathub repository Software offers",
    "duckduckgo.com": "Bloodhound's default web search",
    "www.google.com": "Bloodhound's Google search",
    "www.bing.com": "Bloodhound's Bing search",
}

HOST = re.compile(r"https?://([A-Za-z0-9.-]+)")
SEARCH_SITES = Path("assets") / "Internet Search Sites"


def source_files():
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            if path.is_file() and (path.suffix in SUFFIXES or path.name == "meson.build"):
                yield path
    sites = root / SEARCH_SITES
    if sites.is_dir():
        yield from sites.glob("*.searchsite")


def tracker_hits(files):
    for path in files:
        try:
            text = path.read_text(errors="ignore").lower()
        except OSError:
            continue
        for token, name in TRACKERS.items():
            if token in text:
                yield path, token, name


def network_hosts():
    hosts = {}
    for relative in NETWORK_FILES:
        path = root / relative
        if not path.is_file():
            raise SystemExit(f"privacy: network file not found: {relative}")
        for host in HOST.findall(path.read_text(errors="ignore")):
            hosts.setdefault(host, set()).add(relative)
    sites = root / SEARCH_SITES
    if sites.is_dir():
        for path in sites.glob("*.searchsite"):
            for host in HOST.findall(path.read_text(errors="ignore")):
                hosts.setdefault(host, set()).add(str(path.relative_to(root)))
    return hosts


def main():
    files = list(source_files())
    failures = [f"{path.relative_to(root)}: {name} endpoint ({token})"
                for path, token, name in tracker_hits(files)]

    hosts = network_hosts()
    for host, where in sorted(hosts.items()):
        if host not in ALLOWED_HOSTS:
            failures.append(f"{', '.join(sorted(where))}: undocumented network host {host}")

    policy_path = root / "PRIVACY.md"
    policy = policy_path.read_text(errors="ignore").lower() if policy_path.is_file() else ""
    if not policy:
        failures.append("PRIVACY.md is missing")
    for host, why in ALLOWED_HOSTS.items():
        if host not in policy:
            failures.append(f"PRIVACY.md does not mention {host} ({why})")

    if failures:
        print("ZacOS 9 does not track users; see PRIVACY.md.")
        for failure in failures:
            print("FAIL", failure)
        return 1
    print(f"ok: no tracking endpoints in {len(files)} shipped source files")
    print("ok: the network code uses only the documented hosts: " +
          ", ".join(sorted(hosts)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
