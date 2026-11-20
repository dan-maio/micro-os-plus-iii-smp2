#!/bin/bash
# md2pdf.sh <file.md>...: the PDF beside each Markdown file, as the existing
# ones were made (pandoc -> HTML -> WeasyPrint, A4, titled after the file).
CSS=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/WHY-SMP-BUILD-TEST-DIFFERS.css
for md in "$@"; do
  pdf="${md%.md}.pdf"; title="$(basename "${md%.md}")"
  ( cd "$(dirname "$md")" && pandoc "$(basename "$md")" -s --metadata title="$title" \
      --css "$CSS" -t html5 -o - 2>/dev/null ) | weasyprint - "$pdf" 2>/dev/null \
    && echo "pdf: $pdf ($(pdfinfo "$pdf" 2>/dev/null | awk '/^Pages/{print $2}') pages)" \
    || echo "FAILED: $md"
done
