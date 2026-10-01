#!/bin/sh
# Download Apple's Mac OS 8 HIG and extract every embedded figure as a
# native-resolution PNG into figs/ (named img-PAGE-NUM.png), plus the text
# into hig.txt. Needs poppler-utils and python3-pil.
# These files are reference material only and are git-ignored.
set -eu
cd "$(dirname "$0")"
[ -f hig-os8.pdf ] || curl -sL -o hig-os8.pdf \
	http://interface.free.fr/Archives/Apple_HIGOS8_Guidelines.pdf
mkdir -p figs
pdfimages -png -p hig-os8.pdf figs/img
pdftotext -layout hig-os8.pdf hig.txt
ls figs | wc -l
