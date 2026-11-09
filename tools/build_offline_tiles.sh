#!/usr/bin/env bash
# sārathi — Valhalla routing tiles for the phone's offline router (ADR-0006).
#
# Usage:
#   tools/build_offline_tiles.sh [region]     # default: goa
#
# Downloads <region>-latest.osm.pbf from openstreetmap.fr's extracts and builds one tile file
# with the Valhalla image that matches the app's valhalla-mobile (Valhalla 3.9.1). The output,
# android/app/offline/valhalla/, is gitignored (OSM data, ODbL) and packaged into the app only
# when it exists; a build without it routes online only.
#
# Needs Docker. Touches nothing outside the repo except Docker's image cache.

set -euo pipefail

REGION="${1:-goa}"
VALHALLA_VERSION="3.9.1"  # must match valhalla-mobile's Valhalla (0.6.4 pins 3.9.1)
VALHALLA_IMAGE="ghcr.io/valhalla/valhalla:${VALHALLA_VERSION}"
PBF_URL="https://download.openstreetmap.fr/extracts/asia/india/${REGION}-latest.osm.pbf"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$REPO/android/app/offline/work"
OUT="$REPO/android/app/offline/valhalla"

mkdir -p "$WORK" "$OUT"
rm -rf "$WORK/tiles"

echo "==> Downloading ${REGION} from openstreetmap.fr"
curl -fL --retry 3 -o "$WORK/region.osm.pbf" "$PBF_URL"

echo "==> Building tiles with ${VALHALLA_IMAGE}"
# Run as the current user so the output isn't owned by root. No admin or timezone data: the
# router doesn't need them for plain two-wheeler routes.
docker run --rm --user "$(id -u):$(id -g)" -v "$WORK:/data" "$VALHALLA_IMAGE" bash -euc '
  valhalla_build_config --mjolnir-tile-dir /data/tiles --mjolnir-tile-extract /data/tiles.tar \
    --mjolnir-admin "" --mjolnir-timezone "" > /data/valhalla.json
  valhalla_build_tiles -c /data/valhalla.json /data/region.osm.pbf
  valhalla_build_extract -c /data/valhalla.json -v
'

mv "$WORK/tiles.tar" "$OUT/tiles.tar"
# The app refuses tiles from another Valhalla version (ADR-0006).
printf '%s\n%s\n%s\n' "valhalla=${VALHALLA_VERSION}" "region=${REGION}" "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$OUT/tiles.version"
rm -rf "$WORK"
echo "==> $(du -h "$OUT/tiles.tar" | cut -f1) of tiles in ${OUT#"$REPO"/}"
