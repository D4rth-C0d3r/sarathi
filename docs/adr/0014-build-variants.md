# ADR-0014: Build variants: "foss" and "full"

- **Status:** Accepted (2026-11-14)
- **Date:** 2026-11-14
- **Related:**
  - ADR-0006 (maps backends; keys)
  - ADR-0008 (destination input)
  - ADR-0012 (OSM contributions)
  - ADR-0013 (licensing)
  - ADR-0019 (release)

## Context

The app links two proprietary SDKs (Google's Navigation and Places, Mappls's REST SDK) and
compiles the owner's keys into the build from `~/sarathi-secrets` (ADR-0006, amended
2026-10-31). A public release needs:

- **foss:** a build with no proprietary SDKs, eligible for F-Droid.
- **full:** a build with Google and Mappls.
- **No keys from the owner** in any build: each user enters their own.

### What limits the "full" variant

Each of these was checked in ADR-0006 or in the SDKs themselves.

1. **The Navigation SDK reads its key only from the manifest** (`com.google.android.geo.API_KEY`).
   It has no runtime setter.
   - **No user keys:** a published APK can't take a user's key. It would have to ship one, and the
     owner's keys must never ship.
2. **Google's terms forbid using Google Maps content with a non-Google map.** Drawing Google's
   route on the dash's map was recorded as "the owner's decision for private use" (ADR-0006).
   Distributing that to the public is a different matter.
3. **Mappls's SDK is configured by per-app files** (`<appId>.a.conf`, `.olf`) from the owner's
   Mappls account. A published APK would carry the owner's Mappls credentials.

So **a public "full" APK isn't possible without breaking the "no owner keys" rule or Google's
terms.**

### F-Droid

Checked against F-Droid's Inclusion Policy, Anti-Features and Reproducible Builds pages,
2026-11-14.

- **FLOSS only:**
  - The app must be FLOSS, and so must its whole toolchain.
  - Binary dependencies must come from source builds or trusted repositories.
  - Proprietary SDKs, Google Play services and trackers are excluded.
- **Build flavours:** a flavour without them is the accepted route ("a build flavour that does
  not require these dependencies").
- **Anti-features:** each is a label, not a rejection.
  - **NonFreeNet:** for apps that "promote or depend entirely on a proprietary network service".
  - **TetheredNet:** for apps that depend on one hard-to-replace service. It's not applied when the
    app can be pointed at another instance.
- **API keys:** "F-Droid does not sign up for any API keys."
- **Native code:** Ferrostar (`libferrostar.so`, Rust) and valhalla-mobile
  (`libvalhalla-wrapper.so`, C++) ship prebuilt native libraries in their Maven packages.
  - **The risk:** F-Droid usually wants native code built from source, and reproducible builds are
    hardest for native code.
  - **Open question:** whether F-Droid will build both from source. Ask F-Droid
    (`gitlab.com/fdroid/fdroiddata` issues) before starting the submission.

## Decision

### 1. Two product flavours of `:app`

| | foss | full |
|---|---|---|
| Search | Photon (OSM) | Photon, Google Places, Mappls |
| Routing | Valhalla (FOSSGIS or own server), GraphHopper (user key), offline Valhalla | All of foss, plus Mappls, Google |
| Guidance | Ferrostar | Ferrostar, Google's Navigation SDK |
| Traffic | None (OSM has none) | Mappls, Google |
| Map data | OSM only (OpenFreeMap, offline region) | Same |
| Mappls by plain HTTP | **Optional, behind a user-entered REST key**, if Mappls's REST API and terms allow a third-party app to use it without the SDK (**to verify**); then labelled NonFreeNet | Through the SDK |
| OSM contribution helper | Yes | Yes |
| Modules | No `:maps:google`, no `:maps:mappls` | Everything |
| Who may build it | Anyone; published on GitHub Releases and F-Droid | **Anyone with their own keys, for themselves** (below) |

- **How it's built:** flavour-specific source sets and `fullImplementation(...)` dependencies.
  Shared code reaches backends only through `core:maps`' interfaces (already the rule), so the
  foss flavour simply has no Google or Mappls providers registered.
- **The full flavour isn't published (decided 2026-11-14; the owner left this to Claude).** It's
  documented for users to build themselves:
  - with their own keys in `~/sarathi-secrets`, as the owner does today
  - with their own Google key restricted to their own signing certificate
  - accepting that Google content on the dash map is their own decision, as it is the owner's

  This follows from the Context: a published full APK would carry someone's keys and draw Google
  content on a non-Google map.

### 2. Keys

- **foss:** keys are entered in the app at runtime (GraphHopper, optional Mappls REST), encrypted
  with an Android Keystore key, as `OsmAccount`'s token already is. They're never logged or shown
  after entry.
- **full:** keys stay build-time secrets (ADR-0006), because the Navigation SDK allows nothing
  else.
- **Release builds contain no keys.** A CI check fails a release build if any key field in
  `BuildConfig` or the merged manifest is non-empty. The release job runs without
  `~/sarathi-secrets`, so this guards against a future change that bakes keys in.

### 3. Default servers for a public app

A public app with many users must respect each free service's usage policy.

- **FOSSGIS Valhalla:** fair use; no heavy use.
- **Photon (komoot):** "please don't use for heavy use".
- **OpenFreeMap:** free, no limits stated.

**Decision:**
- **Server choice:** each OSM backend's server is set in Maps settings, as Valhalla's already is,
  so TetheredNet doesn't apply.
- **Defaults:** they stay on the public servers, with a note in the app.
- **Usage policies:** each service's usage policy is checked again before the first public
  release (a release checklist item, ADR-0019).

### 4. Offline data isn't bundled in public builds

- **Downloaded separately:** the offline map region and Valhalla's offline tiles are downloaded
  separately, under ODbL (ADR-0013, ODbL duties).
- **Developer builds:** they may still bundle them (`android/offline/`), as today.

### 5. The Google road extraction (former step 4J) doesn't exist

- **Dropped:** the owner dropped it (2026-11-14); no flag or code exists.
- **If it ever returns as an experimental feature:** it needs its own ADR, and a CI rule then
  keeps it out of every release build. That rule is defined there, not here.
- **What already holds:** ADR-0012's provenance whitelist keeps any non-OSM provenance out of the
  OSM helper.

## Consequences

- **Implementation:** a Phase 5 step:
  - the flavours
  - a runtime key store for foss
  - the CI key check
  - a flavour matrix in CI (both flavours build and test; only foss is released)
  - the transitive licence report (ADR-0013)
- **Owner's daily build:** "full", unchanged.
- **Before the F-Droid submission:**
  - Ask F-Droid about Ferrostar's and valhalla-mobile's native libraries.
  - If they must be built from source, that's a large recipe. Decide then whether F-Droid is worth
    it, or publish the foss APK on GitHub only.
