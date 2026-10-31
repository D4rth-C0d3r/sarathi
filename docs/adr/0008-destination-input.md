# ADR-0008: Destination input

- **Status:** Accepted (2026-10-25)
- **Date:** 2026-10-25
- **Related:** ADR-0006 (backends, cost guard), ADR-0007 (protocol), ADR-0009 (map)

## Context

A ride starts on the phone, before it goes into the pocket. The destination comes either
from searching in the sārathi app, or from another app the owner already uses (Google Maps
most of all, plus Mappls, OSM and Organic Maps) through Android's share sheet or a link.
Nothing about the destination is entered on the device (no interaction while riding).

## Decision

### In-app search

- **Autocomplete** with the selected search backend (ADR-0006):
  - **Google Places (New):** session tokens, so autocomplete requests in a session that ends
    in a Place Details call are free ("Autocomplete Session Usage" is unlimited).
  - **Mappls:** Autosuggest, which also takes a **Mappls Pin** (eLoc) typed directly.
  - **OSM:** Photon, debounced to at most one request per 300 ms, with an identifying
    User-Agent.
  - Typing pauses 300 ms before a request; results are biased to the current location.
- **Recent destinations** and **saved places** (Home, Work, and named favourites), in a
  local database on the phone.
- **Keeping Google content within Google's terms:**
  - A recent destination that came from Google stores the **place ID** indefinitely, but its
    coordinates for at most **30 days**. After that the coordinates are refetched (one Place
    Details call, counted by the cost guard) when it's used again.
  - Places from Mappls, OSM, a map pick or a shared coordinate are stored as they are.
- **Long-press pick on a small preview map**, for planning only:
  - The map is **MapLibre Android with OpenFreeMap tiles**. Google's Maps SDK can't be used
    in the same app as the Navigation SDK.
  - The picked point is reverse-geocoded with the search backend for its name, or kept as
    raw coordinates.
- **Google-derived places never go into OSM data** (hard rule): not into map chunks, and not
  uploaded anywhere.

### Shared links and the share sheet

- **The app registers for:**
  - `ACTION_SEND` with `text/plain`. Shared text is scanned for the first supported URL or
    geo URI.
  - `ACTION_VIEW` for `geo:` and the `https` hosts below. These are offered, not taken by
    default; no auto-verify for hosts we don't own.
- **Short links are resolved by following redirects only:**
  - `HEAD` (falling back to `GET` without reading the body), at most 5 hops, each hop's host
    on the allow-list.
  - **No page scraping.**
  - If the final URL has no coordinates or place, its text query goes to the search backend.

| Source | Formats | What we take |
|---|---|---|
| Google Maps | `maps.app.goo.gl/<id>` and legacy `goo.gl/maps/<id>` (redirect to one of the forms below) | Follow redirects |
| | `/maps/place/<name>/@<lat>,<lng>,<z>z/data=…!3d<lat>!4d<lng>…` | The place's own `!3d`/`!4d` coordinates (not the `@` viewport) and its name |
| | `/maps/search/<query>/@…` and `/maps/search/?api=1&query=<lat,lng or text>[&query_place_id=…]` | A place ID if present (Place Details), else coordinates, else search the text |
| | `/maps/dir/<origin>/<waypoint>/…/<destination>/@…/data=…!3e<mode>` and `/maps/dir/?api=1&origin=…&destination=…&waypoints=a\|b&travelmode=…` | Origin (empty or "Your location" = current position), waypoints in order, destination, travel mode. `api=1` modes are documented: `driving`, `walking`, `bicycling`, `two-wheeler`, `transit`; place IDs may come as `origin_place_id`, `destination_place_id`, `waypoint_place_ids`; links list up to 9 waypoints (3 on mobile browsers). The `!3e` codes in `data=` must be recorded from real shares (below). Names without coordinates are geocoded with the search backend. |
| Mappls | `mappls.com/<Mappls Pin>`, and `maps.mappls.com/…` links (formats to record) | The Mappls Pin, resolved with the Mappls API (key required), or coordinates if present |
| OSM | `openstreetmap.org/?mlat=…&mlon=…#map=z/lat/lon`, `#map=` only, `osm.org/go/<code>` (short code, decoded offline), `openstreetmap.org/directions?route=lat,lon;lat,lon` | Coordinates; a directions route as origin and destination |
| Organic Maps | `omaps.app/<code>/<name>` and `ge0.me/<code>/<name>` (ge0 encoding, decoded offline) | Coordinates, name |
| `geo:` | `geo:<lat>,<lng>`, `geo:<lat>,<lng>?q=<lat>,<lng>(<label>)`, `geo:0,0?q=<address>` | Coordinates and label, or search the address |

- **Formats must be verified, not assumed:**
  - The owner shares a set of **public places and routes** from the phone: Google Maps place,
    search and dir links with waypoints and each travel mode; Mappls place and Pin; OSM; and
    Organic Maps.
  - Each share is recorded as a **test fixture**: the raw shared text, the resolved URL, and
    the expected parse.
  - **Use landmarks, not home or work:** the repository is public.
  - The parser is pure Kotlin in `:core:links` with JVM tests over the fixtures. The network
    redirect step is mocked in tests.

### Recorded formats (2026-10-31)

Shared from the owner's Nothing Phone (1) (Android 15): public landmarks in Goa, stored with
the expected parse in `docs/fixtures/share-links.json`. Short links were resolved from
`Location` headers only.

- **Google place links have no coordinates.** `maps.app.goo.gl/<id>` redirects to
  `/maps/place/<name, address>/data=…!1s0x…:0x…`, with a name, address and Google feature ID
  only.
  - The app looks the name up with the search backend. With Google that's Text Search
    (IDs only, free), then Place Details (counted by the cost guard).
  - A **dropped pin** does carry `!3d<lat>!4d<lng>`.
- **Google routes:** `/maps/dir/<stop>/<stop>/<stop>/data=…`. Each stop has its own
  `!3d`/`!4d`, origin included. **`!3e9` = two-wheeler, `!3e0` = car**; other modes are still
  unrecorded.
- **Google's resolved links carry tracking parameters** (`g_ep`, `coh`, `utm_source`,
  `entry`, `g_st`). The parser ignores them, and fixtures store them stripped.
- **Google search results** have no Share option on the owner's phone, so that format isn't
  needed.
- **Mappls:**
  - `mappls.com/<Pin>` (redirects to `www.`), with the name and address in the shared text;
    the coordinates come from a Mappls Pin lookup.
  - `mappls.com/pom/<lat>%2C<lng>` for a map pin carries its coordinates.
- **OSM:** `#map=z/lat/lon`, `?mlat=…&mlon=…`, and `osm.org/go/<code>[?m=]`. The short code
  decodes offline (OSM's quadtile encoding) to exactly the coordinates osm.org redirects to.
- **Organic Maps:** the shared text includes plain coordinates. `omaps.app/<code>/<name>`
  doesn't redirect; its ge0 code also decodes offline (to within 1 m here).
- **No network needed** for any of these except Google place links (name lookup) and Mappls
  Pins (Pin lookup).

### A shared Google route

- **Origin, waypoints and travel mode are kept,** and routed with the **selected routing
  backend**, so a Google route may be routed by Mappls or OSM if the owner prefers.
- **Matching:**
  - The waypoints are routed in order.
  - With Google, the result is the closest match to the shared route. Google bills **one
    destination per waypoint**, which the cost guard counts.
  - Other backends may differ in detail; the confirmation screen says so ("Routed with OSM;
    may differ from the shared route").
- **Travel mode mapping:**
  - Google `two-wheeler` and `driving` → the backend's two-wheeler profile if it has one,
    else driving.
  - `walking`, `bicycling` and `transit` are shown as "not supported for riding" and fall
    back to two-wheeler, with a note.

### Confirmation screen (before every start)

- **Destination:** name and address; waypoints in order, if any.
- **Route summary:** distance, **traffic-aware ETA** when the backend has traffic, the main
  road ("via NH48"), and travel mode.
- **Backend:** the backend used, its attribution, any fallback that already happened
  ("Google over quota → Mappls"), and the cost-guard status (for example "Google: 312 of
  1,000 destinations this month").
- **Actions:** Start (it starts the ride service and tells the rider the phone can be locked
  now), Change route options, Cancel.
- **Up-front checks:** the device is connected and paired; the map corridor is downloading
  (with progress); background location and battery-optimisation exemption are granted.

## Consequences

- **Destinations come from anywhere the owner already plans rides,** without retyping them.
- **New Android pieces:**
  - `:core:links`, pure Kotlin with fixture tests
  - `:feature:destination` (search, recents, saved places, confirmation)
  - MapLibre Android (new dependency, BSD) for the preview map
- **A fixture-recording session with the owner's phone** is needed before the parser is
  written.
- **Google terms** (30-day coordinate cache, place IDs kept) are followed in storage. The
  owner's decision to draw Google routes on non-Google maps (ADR-0006) also covers the
  preview map.

## Sources

- Google Maps URLs (documented `api=1` parameters and travel modes, checked 2026-10-25):
  [developers.google.com/maps/documentation/urls/get-started](https://developers.google.com/maps/documentation/urls/get-started).
  The undocumented `/maps/place/…/data=` and `/maps/dir/…/data=…!3e…` forms that the app
  actually shares are verified only by the recorded fixtures.
- Places API session pricing: see ADR-0006.
