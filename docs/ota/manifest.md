# OTA manifest, schema 1

The signed description of a firmware release that the device checks before installing it
(ADR-0004). This document is normative. The firmware, `tools/ota_manifest.py` and the
release workflow follow it, and all of them are tested against the golden vectors in
[`vectors/`](vectors/). **Change this document, `manifest.schema.json` and the vectors
first**, then the code.

The key words MUST, MUST NOT and SHOULD are used as in RFC 2119.

## Where it comes from

Each release `vX.Y.Z` on GitHub has two assets:

| Asset | Contents |
|---|---|
| `sarathi-crowpanel21-vX.Y.Z.bin` | The app image, byte-identical to the build's `firmware.bin` |
| `manifest.jws` | This manifest |

The device fetches
`https://github.com/D4rth-C0d3r/sarathi/releases/latest/download/manifest.jws`. GitHub
redirects that URL to the newest non-pre-release.

## Format

A manifest is a **JSON Web Signature** (RFC 7515) in **compact serialisation**:

```text
BASE64URL(UTF8(header)) "." BASE64URL(UTF8(payload)) "." BASE64URL(signature)
```

- The token is ASCII, at most **2048 bytes**. Trailing CR or LF characters are ignored.
- Base64url uses the URL-safe alphabet with **no padding** (RFC 7515 §2).
- Header and payload are UTF-8 JSON objects. **Duplicate member names are rejected**, since
  parsers disagree on which one wins.
- Each part holds at most **128 JSON tokens**, counting one per value and one per member
  name. The device parses into a fixed token table. A release manifest uses 17.

### Header

```json
{"alg":"ES256","typ":"sarathi-manifest+json","kid":"sarathi-ota-1"}
```

| Member | Rule |
|---|---|
| `alg` | MUST be `ES256` (ECDSA P-256 with SHA-256, RFC 7518 §3.4). Anything else, including `none` and `HS256`, is rejected. |
| `typ` | MUST be `sarathi-manifest+json`, so no other JWS can be passed off as a manifest. |
| `kid` | MUST name a key the device trusts (see [`keys/`](keys/)). |
| `crit` | MUST NOT be present. |

Other header members are ignored.

### Signature

ES256 over the ASCII bytes of `header-b64 "." payload-b64`, encoded as the **64-byte raw
`r ‖ s`** form JWS requires (each value 32 bytes, big-endian). DER is not used.

### Payload

```json
{
  "schema": 1,
  "board": "crowpanel21",
  "version": "0.3.0",
  "seq": 300,
  "size": 930480,
  "sha256": "9f2c...64 lowercase hex digits...",
  "min_protocol": 1,
  "url": "https://github.com/D4rth-C0d3r/sarathi/releases/download/v0.3.0/sarathi-crowpanel21-v0.3.0.bin"
}
```

| Member | Type | Rule |
|---|---|---|
| `schema` | integer | `1` for this document. A breaking change raises it. |
| `board` | string | The device class (SUIT's class identifier). `crowpanel21` for the Elecrow CrowPanel 2.1". |
| `version` | string | `MAJOR.MINOR.PATCH`, with MAJOR 0–999 and MINOR and PATCH 0–99, no leading zeros. Pre-release and build suffixes are not allowed. |
| `seq` | integer | Anti-rollback sequence number: MUST equal `MAJOR × 10000 + MINOR × 100 + PATCH`. |
| `size` | integer | Image size in bytes, > 0. |
| `sha256` | string | SHA-256 of the image, 64 lowercase hex digits. |
| `min_protocol` | integer | Lowest phone-protocol version this firmware works with, ≥ 0. Advisory: never blocks the update. |
| `url` | string | Where to download the image. MUST start with `https://`. |

Unknown payload members are ignored, so schema 1 can gain optional fields without breaking
older devices.

- **Integers:** JSON `true` and `false` are not integers, and nor is `4096.0`. Integers
  must be between 0 and 2^53. `NaN` and `Infinity` aren't JSON and are `malformed`.
- **Strings:** every string member MUST be ASCII after JSON unescaping (`\u00e9` is
  rejected).

[`manifest.schema.json`](manifest.schema.json) has the same rules as a JSON Schema.

## Verification

The device MUST apply these steps **in order**, stop at the first failure, and report its
error code. The device MUST NOT act on any payload member before step 3 passes.

| Step | Check | Error code |
|---|---|---|
| 1. Parse | ≤ 2048 bytes, ASCII, three parts (header and payload non-empty), strict base64url, valid JSON objects without duplicate members, ≤ 128 tokens each | `malformed` |
| 2. Header | no `crit` | `malformed` |
| | `alg` is `ES256` | `unsupported_alg` |
| | `typ` is `sarathi-manifest+json` | `unsupported_type` |
| | `kid` is an ASCII string naming a trusted key | `unknown_kid` |
| 3. Signature | 64 bytes, verifies with that key | `bad_signature` |
| 4. Payload | valid JSON (as in step 1); every member present with the right type, strings ASCII; `schema` 1; `seq` matches `version`; `sha256` is 64 lowercase hex; `size` > 0; `min_protocol` ≥ 0 | `bad_schema` |
| | `board` is the device's board | `wrong_board` |
| | `seq` > the running firmware's `seq` | `not_newer` |
| | `size` ≤ the inactive OTA slot | `too_large` |
| | `url` starts with `https://` | `insecure_url` |
| 5. Download | stream into the inactive slot, abort if more than `size` bytes arrive | `size_mismatch` |
| 6. Image | exactly `size` bytes; SHA-256 equals `sha256` | `size_mismatch`, `hash_mismatch` |
| 7. Switch | only now set the boot partition and reboot; on any failure, abort the update without switching | none |

Network and flash failures during download are reported separately: they aren't manifest
errors.

## The running firmware's version

The firmware knows its own version from compile-time constants, `SARATHI_VERSION` and
`SARATHI_SEQ`. The release workflow sets them from the git tag, and local builds use
`0.0.0-dev` and `0`.

The `esp_app_desc_t` version in the image can't be used for this. With the pinned Arduino
core it holds the version of the precompiled framework libraries (`14a0af9`,
`arduino-lib-builder`), not sārathi's.

## Golden vectors

[`vectors/vectors.json`](vectors/vectors.json) lists every case with its expected result,
plus the context to verify in:

- board `crowpanel21`
- running `seq` 200 (0.2.0)
- slot size 6,291,456 B
- the test public key

Each `.jws` file is one manifest. Image cases name an image fixture.

Image cases also record `image_sha256`, the fixture's digest, for tests that can't hash.
Each case also records `signature_valid`:

- `true` or `false`: the result of step 3.
- `null`: verification stopped before step 3.

Tests that can't run ECDSA, such as the firmware's native tests, use it to stand in for the
signature check. Everything else is still tested for real. Real ES256 runs on the device
against the same vectors.

The vectors are signed with a **throwaway key** that existed only while
`tools/ota_manifest.py make-vectors` ran. Its private half was never written to disk, and
its `kid`, `test-only`, is never trusted by firmware. Regenerating the vectors makes a new
throwaway key and replaces every file.

```bash
source .venv/bin/activate
python tools/ota_manifest.py check-vectors docs/ota/vectors   # CI runs this
python tools/ota_manifest.py make-vectors docs/ota/vectors    # after a spec change
```

## Signing a release

`release.yml` (Phase 2) runs, in the protected `release` environment:

```bash
python tools/ota_manifest.py sign --image firmware.bin --version X.Y.Z \
  --url https://github.com/D4rth-C0d3r/sarathi/releases/download/vX.Y.Z/sarathi-crowpanel21-vX.Y.Z.bin \
  --kid sarathi-ota-1 --key-env OTA_SIGNING_KEY --out manifest.jws
```

The key is read from the environment and never printed or written to disk.
