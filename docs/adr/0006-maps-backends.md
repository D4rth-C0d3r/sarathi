# ADR-0006: Maps backends for search, routing and guidance

- **Status:** Accepted (2026-10-25)
- **Date:** 2026-10-25
- **Related:** ADR-0001 (BLE only, phone as brain), ADR-0002 (map), ADR-0005 (Chronos, to be
  replaced), ADR-0007 (protocol v1), ADR-0008 (destination input), ADR-0009 (on-device map)

## Context

Phase 4 replaces Chronos with our own Android app. The app owns search, routing and
guidance; the device only displays what it's sent (ADR-0001). Constraints from the owner:

- **Private project, one rider.** The phone stays **locked in a pocket with the screen off
  for the whole ride**. Everything that runs during a ride must work headless.
- **Free usage only.** Google with a billing account is fine while it stays inside the free
  tier. Any other paid option needs the owner's approval.
- **Google-derived data is never uploaded to OpenStreetMap.**
- Three backends: **Google, Mappls, OSM**. A user-ordered fallback chain, with OSM offline
  always last.

Facts below were checked on 2026-10-25 against the sources listed at the end. Items marked
**verify** need a check in a console or on the phone before Phase 4 relies on them.

## What each backend offers (verified)

### Google

| Item | Finding |
|---|---|
| Navigation SDK billing | SKU "Navigation Request": billed **per destination** in a route request (waypoints × price). Starting guidance, traffic reroutes and off-route reroutes are **not billed**. Requests that reach Google are billed even with `NO_ROUTE_FOUND`; client-side failures are not. |
| Free tier | Global list: **1,000 destinations/month free**, then $25 per 1,000. India list: **7,000/month free**, then $8 per 1,000. The India list doesn't say who qualifies, and the owner couldn't find which list applies in the Console (2026-10-31), so **the cost guard uses the global limits**, the stricter ones. |
| Access | Pay-as-you-go: enable the SDK on a project with billing and an API key. The docs mention "your service agreement" and Mobility terms but don't require a contract (**verify** at enablement). |
| Two-wheeler | `RoutingOptions.TravelMode.TWO_WHEELER` ("motorcycles and scooters"). Regional availability isn't stated in the reference; two-wheeler routing is long established in India (**verify** routes differ from `DRIVING` on a test route). |
| Headless | Google's "Nav SDK headless mode" guide: skip the navigation fragment and map; navigation runs "under the scenes", with location and all event callbacks still firing. Turn-by-turn data comes through the **turn-by-turn data feed**: a bound service receiving `NavInfo` about once a second (maneuver, road name, distance and time to the next step, lanes, remaining time and distance, `ENROUTE`/`REROUTING`/`STOPPED`). Background location needs "Allow all the time"; SDK 5.4.0+ handles Android 14's background rules. **Verify** with screen off and phone in a pocket for a full ride. |
| Notifications | `startGuidance()` starts the SDK's own foreground service with a navigation notification; `setHeadsUpNotificationEnabled(false)` silences heads-up. |
| Route geometry | `Navigator.getRouteSegments()` → `RouteSegment.getLatLngs()`; `RouteChangeListener` on every change. |
| Traffic per segment | `RouteSegment.getTrafficData()` → `NavigationTrafficData` (with a status) → list of `NavigationRoadStretchRenderingData`: **style** (`SLOWER_TRAFFIC`, `TRAFFIC_JAM`, `UNKNOWN`), **offsetMeters**, **lengthMeters** along the polyline. Stretches not listed are free-flowing. Remaining time from `NavInfo` is traffic-aware. |
| Terms | Each user must accept the Navigation SDK terms in a dialog (`NavigationApi.showTermsAndConditionsDialog`). |
| Offline | None ("does not offer an offline mode"); it pre-caches 15–20 minutes of the route. |
| Requirements | Google Play services; Android 7+ (SDK 7.x targets API 36). **The Navigation SDK and the Maps SDK can't be used in the same app.** |
| Places API (New) | Global: Autocomplete Requests 10,000/month free; Place Details Essentials 10,000 free; Place Details Pro 5,000 free; Geocoding 10,000 free. India: 70,000 / 70,000 / 35,000 / 70,000. **Autocomplete Session Usage is unlimited free**: autocomplete requests in a session that ends in a Place Details call aren't charged separately. |
| Terms note | Google's terms forbid using Google Maps content with a non-Google map. **Drawing Google's route geometry on our device map is the owner's decision for private use**, recorded here. Google content may not be cached beyond what the terms allow (place IDs indefinitely, coordinates up to 30 days), and must never reach OSM (hard rule). |

### Mappls (MapmyIndia)

| Item | Finding |
|---|---|
| Plan | Free developer plan; terms noted and accepted by the owner for private use. **No payment method is on the account**, so usage past the free plan can't be billed: Mappls can only reject requests. The free-plan quotas aren't published and the console doesn't show them (checked by the owner, 2026-10-31). |
| Credentials | An "application" registered in the Mappls console with the app's package name and the **SHA-256** of its signing certificate. The console issues two **configuration files** (`<appId>.a.conf` and `<appId>.a.olf`) that authenticate Mappls's **Android SDK**. They're credentials: kept in `~/sarathi-secrets/mappls/` on the owner's laptop, never committed (their names are gitignored in `android/app/` before the integration starts), and copied into the app module by the build. |
| Search | Autosuggest, geocode, reverse geocode, nearby and place details by eLoc (Mappls Pin) in the Search API. **Verify** that place details by Mappls Pin returns coordinates on the free plan. |
| Directions | Route API with geometry and steps. Profile **`biking`** is the two-wheeler profile, only with resource `route_adv`. Traffic-aware resource (`route_traffic`) is **India only**, and `region`/`rtype` aren't supported with the two-wheeler profile. **Verify** whether traffic and `biking` combine, and the step format. |
| Navigation SDK | The public Android SDK lists Routing and Direction Widget but no headless navigation SDK. **Decision: use Directions + our guidance engine (Ferrostar)**, which is headless by construction. Search and Directions are called **through Mappls's Android SDK** (not plain HTTP), because the credentials are SDK configuration files bound to the app's signature. The map component isn't used. |
| Attribution | "Powered by Mappls" shown in the app and on the device while Mappls is active. |

### OSM

| Item | Finding |
|---|---|
| Search | **Photon** (komoot public instance): no published limits; "be fair, extensive usage will be throttled"; no availability guarantee. Supports typeahead. **Nominatim's public server forbids autocomplete** (bans) and allows 1 request/s with an identifying User-Agent, so it's only for occasional reverse geocoding, if at all. |
| Online routing | **GraphHopper** free plan: 500 credits/day, **non-commercial only** (fits private use), forum support, no guarantees; motorcycle profile availability **verify**. **Valhalla** public server (`valhalla1.openstreetmap.de`, FOSSGIS e.V.): terms read by the owner on 2026-10-31. Allowed for non-commercial use like ours, with conditions: at most **1 request/s** and **2 connections**; an identifying **User-Agent** (a library's default isn't enough); a Referer where possible; OSM attribution with a link to <https://www.openstreetmap.org/fixthemap>; the service URL **not hard-coded** (it's a setting with this default); no mass downloading; no availability guarantee, and access can be revoked. Neither has traffic. |
| Offline routing | **GraphHopper on Android is no longer officially supported** (demo removed after 1.0; scoped-storage problems; apps moved away). **BRouter** (MIT, on F-Droid, maintained, offline engine for Locus Map and OsmAnd): 5°×5° segment files; car and custom profiles; integration through its service or local HTTP endpoint. **valhalla-mobile** (MIT): Valhalla compiled as an Android library, routes and map-matching on device tiles; Valhalla's OSRM-format output feeds Ferrostar directly. Size, speed and battery: **measure in a spike** (below). |
| Traffic | None. |

### Guidance engine: Ferrostar

- **Ferrostar** (Stadia Maps): BSD licence, Rust core with Kotlin and Swift libraries; mobile
  "production-ready beta" with no API-stability guarantee yet.
- **Step progression:** configurable conditions (entry/exit distance, distance to end of
  step, custom), or manual.
- **Off route:** a configurable `RouteDeviationDetector`, kept separate from the decision to
  reroute: `RouteDeviationHandler` returns a `CorrectiveAction`. The platform layer makes one
  recalculation request at a time.
- **Route sources:** Valhalla and OSRM-format responses are built in. A `CustomRouteProvider`
  takes routes from anywhere, built from OSRM JSON.
- **Headless:** the core has no UI dependency. Foreground-service handling isn't documented,
  so it runs inside **our** foreground service (**verify** with screen off).

## Decision

### Interfaces (in a new `:core:maps`, pure Kotlin except provider implementations)

```kotlin
interface SearchProvider {
    val id: BackendId               // GOOGLE, MAPPLS, OSM_PHOTON
    val capabilities: SearchCapabilities   // sessions, offline, pin codes (Mappls Pin)
    suspend fun autocomplete(query: String, near: LatLng?, session: SearchSession): List<Suggestion>
    suspend fun details(suggestion: Suggestion, session: SearchSession): Place
    suspend fun reverseGeocode(at: LatLng): Place?
}

interface RoutingProvider {
    val id: BackendId               // GOOGLE_NAV, MAPPLS, OSM_ONLINE, OSM_OFFLINE
    val capabilities: RoutingCapabilities  // traffic, trafficAwareEta, offline, twoWheeler, ownGuidance
    suspend fun route(request: RouteRequest): RouteResult      // geometry, steps, mode, waypoints
    suspend fun reroute(from: Fix, active: Route): RouteResult  // may delegate to route()
    fun traffic(route: Route): Flow<TrafficUpdate>              // empty if no traffic
}
```

- **Common model:** `Route` = polyline (WGS84 internally), steps (maneuver, road name,
  distance), travel mode, waypoints, backend, attribution, ETA, and an optional
  `TrafficUpdate` of `[offsetM, lengthM, level]`.
- **Traffic levels:** `FREE`, `SLOW`, `JAM`, `UNKNOWN`. Google's stretches map directly
  (`SLOWER_TRAFFIC`→`SLOW`, `TRAFFIC_JAM`→`JAM`, `UNKNOWN`→`UNKNOWN`, unlisted→`FREE`);
  Mappls traffic maps once its format is verified; OSM is always `UNKNOWN`.
- **Guidance:** `GuidanceEngine` produces one stream of `GuidanceState` (next maneuver,
  distance, road name, remaining time and distance, off-route and rerouting flags) whatever
  the backend:
  - **Google:** the Navigation SDK's own guidance, read from the turn-by-turn feed and
    route-change and traffic listeners. Its reroutes and traffic updates are free; replacing
    it would bill a destination per reroute.
  - **Mappls and OSM:** Ferrostar, fed through `CustomRouteProvider` (Valhalla/OSRM output
    as is; Mappls converted to OSRM form). Off-route triggers `RoutingProvider.reroute` on
    the same backend, then the fallback chain.

### Settings

- **Maps backend:** one setting picks the default for search *and* routing (Google, Mappls,
  OSM).
- **Advanced:** separate search-backend and routing-backend overrides, and a reorderable
  fallback chain. OSM offline is pinned last and can't be removed.
- **The device shows:** the active routing backend's name and attribution on the nav screen
  at start, and a notice whenever a fallback switches backend ("Google over quota → Mappls").

### Fallback rules

- **Fall back when:** the cost guard blocks; no network; an HTTP or SDK error; a timeout
  (10 s search, 15 s route); or no route found.
- **During a ride:** a reroute goes to the next backend in the chain without asking. The
  confirmation screen only appears before a ride starts (ADR-0008).
- **Never:** route with a backend whose terms or key aren't set up. Skip it and log why.

### Cost guard (per Google API, and per Mappls API once quotas are known)

- **Counts locally,** per billing month (calendar month, Pacific time for Google). Persisted
  in the app's database.
- **What counts:**
  - Navigation: destinations per route request (reroutes not counted).
  - Places: Autocomplete requests outside a completed session, Place Details Essentials and
    Geocoding.
- **Limits:** start from the **global** free caps (1,000 destinations; 10,000 per Places
  SKU). Switch to the India caps only when the owner confirms which list the billing account
  is on.
- **Thresholds:** at **80 %** a warning in the app, plus once on the device at ride start. At
  **95 %** the API is blocked locally until the month resets, and the fallback chain takes
  over.
- **Belt and braces:** daily quota caps in the Cloud Console (below), so a bug in the guard
  can't exceed the free tier either. They can only be set once the billing account is out of
  the free trial (the trial blocks quota edits, but it also can't charge).
- **Mappls (quotas unknown):**
  - **Detect, don't predict:** a quota or rate-limit error from Mappls blocks it for the rest
    of the day, and the fallback chain takes over. The device shows "Mappls over quota → OSM".
  - **Conservative local caps** against runaway use: **200 search** and **100 routing calls a
    day** by default, adjustable in advanced settings.
  - **Usage is counted per day** and shown in the app's settings, so real figures replace the
    guesses once known.
- **Google billing errors** (trial ended, billing disabled) are reported as "Google
  unavailable (account)", not as a generic failure, and fall back like a quota block.

### API keys

- **Entry and storage:** keys are entered in the app's settings and encrypted with an
  **Android Keystore** key (AES-GCM; the Keystore key never leaves the hardware-backed store).
- **Never:** committed, logged, put in crash reports or sent to the device.
- **Google key** restricted to the app's package and signing-certificate SHA-1, and to the
  Navigation SDK and Places API (New) only.

### Headless operation (verify each on the phone, screen off, phone in a pocket)

| Part | How it stays alive | Verify |
|---|---|---|
| BLE link, guidance, location | One app foreground service (types `connectedDevice` and `location`) for the whole ride; "Allow all the time" location; battery-optimisation exemption requested (private app) | 1-hour ride, screen off, Doze, on the owner's **Nothing Phone (1), Android 15** (Nothing OS; July 2026 security patch) |
| Google Navigation SDK | Its own guidance service plus our turn-by-turn feed service | Feed keeps arriving with the screen off; no Activity needed after start |
| Mappls Directions, GraphHopper, Valhalla (HTTP) | Called from our foreground service, which keeps network access in Doze | Reroute succeeds with the screen off |
| Ferrostar, offline router | Run inside our foreground service | Off-route → reroute with the screen off |
| Search (any backend) | Only used before a ride, with the screen on | — |

### Offline OSM router: valhalla-mobile (spike, 2026-10-25)

**Region: Goa** (the owner's). OSM extract: `goa-latest.osm.pbf` from openstreetmap.fr,
7.5 MB.

| | valhalla-mobile (Valhalla 3.9.1) | BRouter 1.7.10 |
|---|---|---|
| Version pin | valhalla-mobile 0.6.4 (2026-10-06) pins Valhalla **3.9.1**; tiles built with the matching `ghcr.io/valhalla/valhalla:3.9.1` image | Release jar |
| Data for Goa | **14 MB** of routing tiles (18 files), built in 3 s on the laptop. Optional: admin boundaries 6.9 MB (country rules), global timezones 117 MB (only for time-dependent routing; can be left out or cut down) | **21.9 MB**: two prebuilt 5°×5° segments (`E70_N10`, `E70_N15`) that also cover most of Maharashtra and Karnataka. Smaller means building our own. |
| Motorcycle profile | `motorcycle` costing. `motor_scooter` is unsuitable: 7–12 km detours avoiding main roads. | No motorcycle profile. `car-fast` is the closest; `moped` assumes about 45 km/h. |
| Routes (5 Goa trips) | Same corridors as BRouter `car-fast` within 1 km, except Panaji–Mollem (+5 km, prefers the highway) | — |
| Compute time (laptop, warm) | **13–16 ms** | 110–490 ms |
| ETA (no traffic) | Optimistic (Panaji–Margao 30 min) | More realistic (40 min) |
| Turn instructions | OSRM-format steps, read **directly by Ferrostar** | "Voice hints"; would need a converter to Ferrostar's format |

**Decision: valhalla-mobile with the `motorcycle` costing.** It's faster, has smaller
per-region data, outputs what Ferrostar reads natively, and can map-match. The optimistic
ETA matters little for the last fallback, and the dash also shows the remaining distance.

**Agreed with the owner:**
- **No BRouter test on the phone:** it couldn't change the outcome.
- **Phone smoke test** (route time and memory) when the offline provider is built.
- **Battery** is measured in Phase 4's full screen-off ride test. Routing only runs on a
  reroute, so its cost is small next to GPS and BLE.
- **Valhalla and its tiles stay on the same version;** the app checks the tile version when
  loading.

## Recommended defaults

| Setting | Default | Why |
|---|---|---|
| Routing chain | **Google → Mappls → OSM online (Valhalla, then GraphHopper) → OSM offline** | Google has the best two-wheeler routing and traffic in India, within the free tier for one rider; Mappls is strong in India and has traffic; OSM online needs no key; offline is last by rule |
| Search | **Google Places (session-based)**, then Mappls, then Photon | Session autocomplete is free; Place Details stays far inside the free cap for one rider; Mappls understands Indian addresses and Mappls Pins |
| Guidance | **Google Navigation SDK's guidance when routing with Google; Ferrostar otherwise** | Keeps Google's free traffic reroutes; one open engine for everything else |
| Travel mode | Two-wheeler where the backend has it (Google `TWO_WHEELER`, Mappls `biking`); else driving | Motorcycle |

**Expected Google usage** for one rider: about 2–4 destinations a day (60–120 a month) and
a few dozen place lookups a month. That's about 10 % of the global Navigation cap and under
1 % of the Places caps.

## Owner's steps in Google Cloud Console (not automated)

1. **Project and billing:** create a project (for example "sarathi") and link the billing
   account.
2. **Enable APIs:** APIs & Services → Library → enable **Navigation SDK** and **Places API
   (New)**. Leave the Maps SDK disabled: it can't be used together with the Navigation SDK
   anyway.
3. **Create the key:** APIs & Services → Credentials → Create credentials → API key.
4. **Restrict the key's application:** Edit the key → Application restrictions → **Android
   apps** → add package `io.github.d4rthc0d3r.sarathi` with the **SHA-1** of each signing
   certificate you use.
   - Debug: `keytool -list -v -keystore ~/.android/debug.keystore -alias androiddebugkey -storepass android -keypass android`.
   - Release: the same, with the release keystore.
   - Or run `./gradlew signingReport` in `android/`.
5. **Restrict the key's APIs:** API restrictions → **Restrict key** → only Navigation SDK
   and Places API (New).
6. **Daily quota caps,** once the billing account is **out of the free trial** (the trial
   blocks quota edits but can't charge; the APIs just stop when it ends). Google Maps
   Platform → **Quotas** (<https://console.cloud.google.com/project/_/google/maps-apis/quotas>)
   → pick the API → tick a per-day quota → **Edit quota**:
   - Navigation SDK: **30 requests/day** (1,000 ÷ 31 with margin).
   - Places API (New): Autocomplete **300/day**, Place Details **300/day** (10,000 ÷ 31). If
     only per-minute quotas are listed, set those to about 10/min.
   - Raise them only if the owner moves to the India caps.
7. **Billing alert:** Billing → **Budgets & alerts** → **Create budget**.
   - Scope: the sārathi project only; **untick all credits**, or the trial credit hides real
     usage.
   - Amount: a small figure (for example ₹100).
   - Email alerts at 50 %, 90 % and 100 % of actual spend.
   - A budget only alerts; the quota caps are what actually limit spending. Done by the owner
     on 2026-10-31, as were steps 1–5.
8. **Paste the key** into the app's settings once that screen exists (Phase 4). It's
   encrypted with the Keystore and never leaves the phone except in requests to Google. Until
   then the key stays only in the Console.

## Consequences

- **The app gets more dependencies,** approved by the owner on 2026-10-31 (each is still
  flagged when it's added, with its exact version):
  - Google Navigation SDK and Places SDK (Play services)
  - Mappls Android SDK (search and directions only) and the Mappls Maven repository
  - Ferrostar
  - the offline router, valhalla-mobile (0.6.4, Valhalla 3.9.1)
  - an HTTP client and `kotlinx.serialization`
- **The app needs Google Play services** for the Google backend; OSM and Mappls work
  without it.
- **One foreground service** owns the ride: BLE, guidance and location.
- **Verification items still open:** Mappls formats (Mappls Pin lookup, `biking` with
  traffic, step format), checked when the Mappls provider is built; and headless behaviour on
  the owner's phone, in the screen-off ride test. Done: the offline-router spike, the FOSSGIS
  terms, the Google Console steps 1–5 and 7, and the price list (global limits, by default).
  Mappls quotas are unknowable up front and are handled by detection instead.

## Sources (checked 2026-10-25)

- Google: [Navigation SDK usage and billing](https://developers.google.com/maps/documentation/navigation/android-sdk/usage-and-billing),
  [pricing](https://developers.google.com/maps/billing-and-pricing/pricing),
  [India pricing](https://developers.google.com/maps/billing-and-pricing/pricing-india),
  [Navigation SDK pricing change (2025-05-01)](https://mapsplatform.google.com/resources/blog/making-navigation-sdks-more-accessible-with-new-pricing/),
  [TravelMode](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/navigation/RoutingOptions.TravelMode),
  [turn-by-turn feed](https://developers.google.com/maps/documentation/navigation/android-sdk/tbt-feed),
  [headless mode](https://developers.google.com/maps/architecture/navsdk-headless-mode),
  [background location](https://developers.google.com/maps/documentation/navigation/android-sdk/background-location-usage),
  [FAQ](https://developers.google.com/maps/documentation/navigation/android-sdk/faq),
  [RouteSegment](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/navigation/RouteSegment),
  [NavigationTrafficData](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/navigation/NavigationTrafficData),
  [road stretch data](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/navigation/NavigationRoadStretchRenderingData),
  [setup](https://developers.google.com/maps/documentation/navigation/android-sdk/set-up-project)
- Mappls: [Android SDK](https://github.com/mappls-api/mappls-android-sdk),
  [direction plugin docs (biking, traffic)](https://pub.dev/documentation/mappls_direction_plugin/latest/)
- OSM: [Photon](https://photon.komoot.io/),
  [Nominatim policy](https://operations.osmfoundation.org/policies/nominatim/),
  [GraphHopper pricing](https://www.graphhopper.com/pricing/),
  [GraphHopper offline status](https://forum.kurviger.com/t/app-offline-routing-graphhopper-deprecation/4334),
  [BRouter](https://github.com/abrensch/brouter),
  [valhalla-mobile](https://github.com/Rallista/valhalla-mobile)
- Ferrostar: [guide](https://stadiamaps.github.io/ferrostar/),
  [route providers](https://stadiamaps.github.io/ferrostar/route-providers.html),
  [navigation controller](https://stadiamaps.github.io/ferrostar/configuring-the-navigation-controller.html)
