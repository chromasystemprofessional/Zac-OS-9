#!/bin/sh
# List which HIG figures appear on which PDF page, with the embedded images
# on that page. usage: figures.sh [CHAPTER]   e.g. figures.sh 4
set -eu
cd "$(dirname "$0")"
chapter=${1:-[0-9]+}
pages=$(pdfinfo hig-os8.pdf | awk '/^Pages:/ {print $2}')
for p in $(seq 1 "$pages"); do
	figs=$(pdftotext -f "$p" -l "$p" hig-os8.pdf - |
		grep -oE "Figure $chapter-[0-9]+" | sort -u | tr '\n' ' ')
	[ -n "$figs" ] || continue
	imgs=$(ls figs 2>/dev/null | grep -E "^img-0*$p-" | tr '\n' ' ' || true)
	echo "page $p: $figs| $imgs"
done
