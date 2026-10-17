#!/usr/bin/env bash
# Copies site/ to an output folder with the contact details filled in. Those
# three values are the publisher's own and are deliberately not in the repo.
#   ./scripts/build-site.sh <out-dir> "<legal name>" "<postal address>" "<email>"
set -euo pipefail
[[ $# -eq 4 ]] || { echo "usage: $0 <out-dir> <legal name> <postal address (may be empty)> <email>"; exit 1; }
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"
rm -rf "$OUT"; mkdir -p "$OUT"
cp -R "$ROOT/site/." "$OUT/"
esc() { printf '%s' "$1" | sed -e 's/[\/&|]/\\&/g'; }
# No postal address given: drop it (and its comma) rather than print an empty one.
if [[ -z "$3" ]]; then
  find "$OUT" -name '*.html' -exec sed -i '' -e 's|, {{ADDRESS}}||g' {} +
fi
find "$OUT" -name '*.html' -exec sed -i '' \
  -e "s|{{LEGAL_NAME}}|$(esc "$2")|g" -e "s|{{ADDRESS}}|$(esc "$3")|g" -e "s|{{EMAIL}}|$(esc "$4")|g" {} +
if grep -rq '{{' "$OUT"; then echo "unfilled placeholder left in $OUT" >&2; exit 1; fi
echo "site written to $OUT"
