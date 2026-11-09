# Map fixtures (ADR-0009)

Recorded vector tiles and the map chunks made from them, for the device's map renderer
(Phase 4 step G1) and the phone's map pipeline (step G2), whose Kotlin output must match these
chunks byte for byte.

**Data © OpenStreetMap contributors, under the [ODbL](https://opendatacommons.org/licenses/odbl/);
tiles © [OpenMapTiles](https://openmaptiles.org/), served by [OpenFreeMap](https://openfreemap.org/).**

## Tiles: `openfreemap-20261101/`

OpenFreeMap's planet build `20261101_080013_pt`, fetched on 2026-11-09, one file per tile
named `z-x-y.pbf`. Only the layers chunks use (`transportation`, `water`, `park`) are kept,
copied byte for byte (`tools/make_chunks.py filter`).

| Set | Tiles | What |
|---|---|---|
| Bengaluru centre | z14 x 11722–11724, y 7595–7597 | A dense city, 3×3 L0 tiles (about 7 km square) |
| Mumbai–Pune expressway | z12 x 2881–2883, y 1830; z14 x 11530–11532, y 7322 | The ghat section: L1 tiles along it, L0 tiles at the ghat |

## Chunks: `chunks/`

Made with `tools/make_chunks.py chunks` (data version `20261101`, provenance
`osm_openfreemap`, epoch 1):

```bash
python tools/make_chunks.py chunks --frame 12.97,77.59 --data-version 20261101 \
    --out docs/fixtures/map/chunks/bengaluru docs/fixtures/map/openfreemap-20261101/14-1172*.pbf
python tools/make_chunks.py chunks --frame 18.75,73.40 --data-version 20261101 \
    --out docs/fixtures/map/chunks/expressway docs/fixtures/map/openfreemap-20261101/12-*.pbf \
    docs/fixtures/map/openfreemap-20261101/14-1153*.pbf
```

| Set | Chunks | Size |
|---|---|---|
| Bengaluru | 9 × L0: 935–1,367 lines, 2,890–4,627 points, 9.6–14.5 KB each | 101 KB |
| Expressway | 3 × L1 (1.2–1.6 KB), 3 × L0 (1.1–3.3 KB) | 11 KB |
