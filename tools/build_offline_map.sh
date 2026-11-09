#!/usr/bin/env bash
# sārathi — the offline map region for the phone (ADR-0009 "Data on the phone").
#
# Usage:
#   tools/build_offline_map.sh [region]     # default: goa
#
# Extracts the region from Protomaps' newest daily planet build (build.protomaps.com keeps about
# a week of them) with the go-pmtiles image, up to zoom 14 (the L0 chunks' tiles). The output,
# android/app/offline/map/, is gitignored (OSM data under the ODbL; Protomaps' schema is BSD)
# and packaged into the app only when it exists; without it, the map comes from OpenFreeMap
# online only. Only the region's tiles are downloaded, not the planet (HTTP range requests).
#
# Needs Docker. Touches nothing outside the repo except Docker's image cache.

set -euo pipefail

REGION="${1:-goa}"
PMTILES_IMAGE="protomaps/go-pmtiles:v1.31.2"
MAX_ZOOM=14

# Bounding boxes: min lon, min lat, max lon, max lat.
case "$REGION" in
  goa) BBOX="73.65,14.88,74.35,15.82" ;;
  *)
    echo "unknown region: $REGION (add its bounding box to this script)" >&2
    exit 1
    ;;
esac

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/android/app/offline/map"
mkdir -p "$OUT"

# The newest daily build: today's may not be out yet.
BUILD=""
for days_ago in 0 1 2 3 4 5; do
  candidate="$(date -u -d "-${days_ago} days" +%Y%m%d)"
  if curl -sfI "https://build.protomaps.com/${candidate}.pmtiles" > /dev/null; then
    BUILD="$candidate"
    break
  fi
done
if [[ -z "$BUILD" ]]; then
  echo "no Protomaps build found in the last few days" >&2
  exit 1
fi

echo "==> Extracting ${REGION} (${BBOX}) from Protomaps build ${BUILD}, zoom 0-${MAX_ZOOM}"
docker run --rm --user "$(id -u):$(id -g)" -v "$OUT:/data" "$PMTILES_IMAGE" \
  extract "https://build.protomaps.com/${BUILD}.pmtiles" /data/region.pmtiles \
  --bbox="$BBOX" --maxzoom="$MAX_ZOOM"

# The app reads the data version (the build date) from here; chunks carry it (ADR-0009).
printf '%s\n%s\n%s\n' "build=${BUILD}" "region=${REGION}" "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$OUT/region.version"
echo "==> $(du -h "$OUT/region.pmtiles" | cut -f1) of map in ${OUT#"$REPO"/}"
