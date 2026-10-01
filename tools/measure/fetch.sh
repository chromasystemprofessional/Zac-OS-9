#!/bin/sh
# Download Apple's Mac OS 8 HIG and extract the chapter-5 window figures as
# native-resolution PNGs into figs/. Needs poppler-utils and python3-pil.
# These files are reference material only and are git-ignored.
set -eu
cd "$(dirname "$0")"
[ -f hig-os8.pdf ] || curl -sL -o hig-os8.pdf \
	http://interface.free.fr/Archives/Apple_HIGOS8_Guidelines.pdf
mkdir -p figs
pdfimages -png -p -f 95 -l 115 hig-os8.pdf figs/img
ls figs
