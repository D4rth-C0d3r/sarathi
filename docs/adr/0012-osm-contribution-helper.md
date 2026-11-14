# ADR-0012: An opt-in OSM contribution helper for missing roads

- **Status:** Accepted (2026-11-14)
- **Date:** 2026-11-14
- **Related:** ADR-0006 (maps backends, Google terms), ADR-0008 (saved places), ADR-0009 (map
  chunks and provenance), ADR-0010 (the ride link), CLAUDE.md "Hard rules"

## Context

The dash's map comes from OpenStreetMap (ADR-0009). Where the rider goes, OSM sometimes has no
road: new roads, village lanes, estate roads. The rider is the person best placed to fix that,
having just ridden it, but only after the ride, and only in ways the OSM community accepts.

The owner asked for a helper in the style of StreetComplete: off by default, using only the
rider's own GPS track and OSM data, never Google or Mappls data, and never editing on its own.
After a ride, each candidate gap gets one of three answers: upload the GPS trace, leave a note
for local mappers, or add the road from the trace with a road type the rider picks. All are
made with the rider's own OSM account, and each is confirmed by the rider.

### The rules this follows

Read on 2026-11-14:

- **[Automated Edits code of conduct][aecoc]:** it covers "all edits where changes are made to
  objects in the database without review individually by the person controlling the edits":
  bots, imports, scripted changes, and find-and-replace "without reviewing each object
  individually".
  - Here every edit is one object that the rider reviews on a map and confirms. That makes them
    manual edits, outside the code's scope.
  - The code still asks for discussion when in doubt. The low volume, one rider, and the
    changeset tags below keep this transparent.
  - Anything that would edit without that per-edit review, or in bulk, is out of scope for this
    ADR. It would need the code's full process (documentation, a forum discussion, a dedicated
    account).
- **[Organised Editing Guidelines][oeg]:** these "apply to any edits that involve more than one
  person". This is one person mapping as themselves, so they don't apply. If the helper is
  ever offered to others as a campaign, they will: a wiki page and a hashtag.
- **[API usage policy][api-policy]:**
  - "The editing API is provided in order to edit the map data, not for read-only purposes."
    So reads go to Overpass, and the editing API is called only to make an edit, including its
    small `map` download.
  - A valid User-Agent that names the app and its version ("Faking another app's User-Agent WILL
    get you blocked").
  - At most 2 download threads.
  - No personal data in submissions.
- **[Overpass API usage][overpass]:**
  - Up to about 10,000 requests and 1 GB a day.
  - On 429, resubmit after 15 s; 504 means the query was too big.
- **[Good practice][good-practice]:**
  - "You must not use other maps, including Google Maps and other commercial providers, as a
    source for changes."
  - "The accuracy of the points in a single GPS trace may be out by several meters."
  - Upload GPS traces so errors can be averaged out.
  - "Always mark your estimations with a fixme."
- **[Contributor Terms][ct] 1(a):** the contributor warrants "You have the right to authorize
  OSMF to use and distribute those Contents". Here the contents are only the rider's own track
  and observations.
- **[Changesets][changeset]:**
  - `comment` must be "a meaningful, human-generated description (not an automated message)".
  - `created_by` names the software.
  - `source` goes on the changeset.
  - Keep changesets local.
- **[`source=*`][source]:**
  - `survey`: "The OSM user was there and saw it firsthand".
  - `gps`: measured with a satellite receiver.
  - Several values are joined with `;`.
- **[Notes][notes]:**
  - "Don't create automated notes. Notes should be a human-to-human communication."
  - Be clear and place the note accurately.
  - AI-generated notes are closed as spam.
- **[GPS traces][api] and their [visibility][visibility]:**
  - `POST /api/0.6/gpx`.
  - Since August 2026 only `trackable` and `identifiable` are accepted (`public` and `private`
    are deprecated and refused).
  - "There is no way to make a track completely private."
- **[OAuth 2][oauth]:**
  - Authorization code with PKCE.
  - Scopes `write_api`, `write_gpx`, `write_notes`.
  - A non-confidential client for a mobile app.
  - Redirect URIs must be https or `http://127.0.0.1`.
  - A separate dev server (`master.apis.dev.openstreetmap.org`) with its own accounts.

## Decision

### 1. Off by default; turning it on explains what is shared

A switch in the app, under Maps settings. Turning it on shows, in plain words:

- what is recorded: the raw GPS track of rides, on the phone only
- what leaves the phone, and only when the rider chooses it for a gap: a GPS trace (public on
  OSM, linked to the rider's account if identifiable), a note (public, signed by the account),
  or an edit (public, signed)
- that OSM keeps all of it public for good

Turning it off stops recording and deletes the stored tracks.

### 2. Only the rider's own GPS, only OSM data (enforced in code)

- **The track is raw GNSS:** `LocationManager.GPS_PROVIDER`, in its own listener during a ride.
  - It is never the fused provider, which mixes in Google's Wi-Fi and cell positions.
  - It is never a position snapped or matched by Google's Navigation SDK.
  - It is never route geometry from any backend.
- **Fixes are filtered:** a fix is kept only with an accuracy of 15 m or better and a plausible
  speed. Gaps are never drawn across missing fixes.
- **A type of its own:** track points are a type (`RawGpsFix`) that only the GPS listener
  creates.
- **The roads come only from OSM-provenance map chunks** (ADR-0009):
  - `OSM_OPENFREEMAP`, or `OSM_PROTOMAPS_OFFLINE`.
  - The detector takes a road network built through a whitelist of `Provenance` values.
  - A test fails when a `Provenance` value isn't classified, so a future non-OSM source can't
    slip in.
- **The module can't reach Google or Mappls:**
  - The contribution module (`:core:osmcontrib`, pure Kotlin) doesn't depend on `:maps:google`,
    `:maps:mappls` or `:core:maps`' route types. A build test checks those dependencies.
  - Search results, routes and traffic from any backend never reach it.
- **Live OSM confirms each gap.** After the ride, every candidate is checked against current OSM
  through Overpass (OSM data, ODbL). That is all data the rider may use.

### 3. Finding gaps, after the ride and never during it

During a ride, the only addition is the raw track, stored in app-private storage. Nothing shows
on the dash and nothing in the app, and the phone stays in the pocket.

Once the ride has ended:

1. **Map data along the track:**
   - The phone makes chunks for the whole track, not only the route corridor, from the tile cache
     or OpenFreeMap or the offline region.
   - Where no map data can be had, the stretch is unknown, never a gap.
2. **Candidates:**
   - The track is resampled every 10 m.
   - A candidate is a run of at least 150 m whose points are all more than 25 m from any chunk
     road at the most detailed chunk level.
   - The 25 m allows for GPS error plus the tiles' simplification.
3. **Confirmed with Overpass:**
   - One small query per candidate: any `highway=*` (including track, path, construction and
     proposed) within 25 m, and `amenity=parking`/`fuel` areas.
   - Chunks keep only motorway to service roads, so this removes tracks and paths that exist,
     roads added since the tiles were built, and car parks.
   - The query carries the app's User-Agent, and a 429 is retried once after 15 s.
4. **Results:**
   - What survives is listed on a small map drawn from the same chunks, with the track over it.
   - The rider can skip a gap (ask again on a later ride) or dismiss it for good. Dismissed places
     are stored as small areas and never offered again.

### 4. What the rider can do with a gap, each confirmed

- **Upload the GPS trace:**
  - The stretch of the ride around the gap, as GPX with timestamps.
  - **Privacy cut:** points within 300 m of the ride's start, its end and every saved place
    (ADR-0008) are removed before upload. The distance can be changed in settings.
  - **Visibility:** `identifiable` by default, `trackable` as the choice. These are the only two
    OSM accepts.
  - The description says it was recorded with sārathi, and the rider can edit it.
  - Tags: `sarathi`.
- **Add an OSM note:**
  - Placed on the middle of the gap.
  - The text is the rider's: the app offers a starting draft ("I rode a road here that the map
    doesn't have: <length>, <date>") that the rider edits. It is sent only after the rider reads
    it.
  - Nothing generated beyond that draft.
- **Guided edit**, the last part to be built, with stricter limits:
  - The way is the simplified trace (Douglas-Peucker, 5 m), at most 2,000 nodes.
  - `highway=*` is picked by the rider from a short list: residential, unclassified, service,
    track, path.
  - `fixme=position from one GPS trace; check against imagery` is added, following Good
    practice.
  - Both ends must join existing roads within 25 m; otherwise only a note is offered. They join
    an existing node, or a new node on the existing way, from a fresh `map` download of the small
    area.
  - At most 5 km long.
  - The rider sees the new road on the map with its connections before confirming.
  - The changeset carries `review_requested=yes`.

### 5. Changesets, rate limits and the account

- **Account:** the rider's own, through OAuth 2 with PKCE.
  - The OAuth app is registered in the rider's osm.org account, with scopes `write_api`,
    `write_gpx` and `write_notes`.
  - The client ID lives outside the repo, like the other keys.
  - The token is kept encrypted with the Android Keystore.
- **Redirect URI:** a custom scheme (`sarathi://osm-oauth`) if osm.org accepts it at
  registration, as some mobile editors use. Otherwise a one-shot loopback listener on
  `http://127.0.0.1:<port>`, which the OAuth page allows.
- **Changeset tags:**
  - `comment`: offered as a draft ("Add a road surveyed by GPS on a ride"), and the rider edits or
    confirms it. It is never an automated message.
  - `created_by=sarathi <app version>`
  - `source=survey;gps`
  - `review_requested=yes` on guided edits
  - One changeset per edit, closed right after; each edit is local.
- **Rate limits** (ours, on top of OSM's):
  - at most 5 edits, 5 notes and 5 traces a day
  - 1 request at a time to OSM and to Overpass
  - backing off on 429, 503 and 509
- **User-Agent:** `sarathi/<version>`: the app and its version, as the policy asks, with no
  personal details. A contact address can be added later if the owner wants one.
- **Dev server first:** each step is tested against `master.apis.dev.openstreetmap.org` before
  the live server is enabled. The live server is enabled by a build setting, not by a runtime
  switch.

### 6. What it doesn't do

- No edits without a confirmation per edit, and no batch confirmations.
- No changes to existing tags or geometry beyond joining the new way's ends.
- No deletions.
- No editing of names, speeds, surfaces or anything not seen on the ride.
- Nothing during a ride, and nothing on the dash.
- No protocol change: positions never reach the dash.

## Consequences

- **Steps:**
  - I1: the switch, its explanation and raw track recording.
  - I2: gap finding and the map.
  - I3: OSM login, traces and notes.
  - I4: the guided edit.
- **Module:** a new pure-Kotlin module `:core:osmcontrib` holds detection, GPX, the osmChange
  builder and the rate limiter, with JVM tests.
- **No new dependencies expected:**
  - the browser for login
  - OkHttp, already in the app, for HTTP
  - the built-in XML parser
  - a Canvas map from the chunks

  Anything else is flagged first.
- **Storage:** tracks take about 100 KB an hour, kept 30 days or until the gaps are handled.
- **Battery:** GPS is already on during a ride, so a second listener costs little.
- **Owner setup:**
  - Register the OAuth app on the dev server and on osm.org.
  - Create a dev-server account.
- **Hard rule in CLAUDE.md:** Google data never goes to OSM. It now has code behind it: the
  provenance whitelist, the raw-GPS type, and the module boundary test.

[aecoc]: https://wiki.openstreetmap.org/wiki/Automated_Edits_code_of_conduct
[oeg]: https://osmfoundation.org/wiki/Organised_Editing_Guidelines
[api-policy]: https://operations.osmfoundation.org/policies/api/
[overpass]: https://dev.overpass-api.de/overpass-doc/en/preface/commons.html
[good-practice]: https://wiki.openstreetmap.org/wiki/Good_practice
[ct]: https://osmfoundation.org/wiki/Licence/Contributor_Terms
[changeset]: https://wiki.openstreetmap.org/wiki/Changeset
[source]: https://wiki.openstreetmap.org/wiki/Key:source
[notes]: https://wiki.openstreetmap.org/wiki/Notes
[api]: https://wiki.openstreetmap.org/wiki/API_v0.6
[visibility]: https://wiki.openstreetmap.org/wiki/Visibility_of_GPS_traces
[oauth]: https://wiki.openstreetmap.org/wiki/OAuth
