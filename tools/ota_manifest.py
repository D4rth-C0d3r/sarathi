#!/usr/bin/env python3
"""sārathi OTA manifest tool (docs/ota/manifest.md, ADR-0004).

A manifest is a JWS (RFC 7515, compact serialisation) signed with ES256. This script is the
reference implementation of the verification rules in the spec, and the signer used by the
release workflow.

Commands:
  sign           build the payload for an image and sign it
  verify         check a manifest (and optionally an image) against the rules
  make-vectors   regenerate the golden test vectors with a throwaway test key
  check-vectors  check every golden vector gives its expected result

The signing key is read from a file or an environment variable and is never printed.
Run from the project .venv (pip install -r tools/requirements.txt).
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import (
    decode_dss_signature,
    encode_dss_signature,
)

# ---- Constants from docs/ota/manifest.md ----

TYP = "sarathi-manifest+json"
ALG = "ES256"
SCHEMA = 1
MAX_TOKEN_BYTES = 2048
DEFAULT_BOARD = "crowpanel21"
P256_COORD_BYTES = 32

# Error codes, shared with the firmware tests through the vectors.
MALFORMED = "malformed"
UNSUPPORTED_ALG = "unsupported_alg"
UNSUPPORTED_TYPE = "unsupported_type"
UNKNOWN_KID = "unknown_kid"
BAD_SIGNATURE = "bad_signature"
BAD_SCHEMA = "bad_schema"
WRONG_BOARD = "wrong_board"
NOT_NEWER = "not_newer"
TOO_LARGE = "too_large"
INSECURE_URL = "insecure_url"
SIZE_MISMATCH = "size_mismatch"
HASH_MISMATCH = "hash_mismatch"
OK = "ok"

VERSION_RE = re.compile(r"^(0|[1-9]\d{0,2})\.(0|[1-9]\d?)\.(0|[1-9]\d?)$")  # 0-999 . 0-99 . 0-99
B64URL_RE = re.compile(r"^[A-Za-z0-9_-]*$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
REQUIRED = {
    "schema": int,
    "board": str,
    "version": str,
    "seq": int,
    "size": int,
    "sha256": str,
    "min_protocol": int,
    "url": str,
}


class OtaError(Exception):
    def __init__(self, code: str, detail: str = ""):
        super().__init__(f"{code}: {detail}" if detail else code)
        self.code = code


@dataclass
class Context:
    board: str
    running_seq: int
    slot_size: int
    keys: dict[str, ec.EllipticCurvePublicKey] = field(default_factory=dict)


# ---- Encoding helpers ----


def b64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def b64url_decode(text: str) -> bytes:
    # Strict: URL-safe alphabet only, no padding, no impossible lengths.
    if not B64URL_RE.match(text) or len(text) % 4 == 1:
        raise OtaError(MALFORMED, "invalid base64url")
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def _no_duplicates(pairs: list[tuple[str, object]]) -> dict:
    keys = [k for k, _ in pairs]
    if len(keys) != len(set(keys)):
        raise OtaError(MALFORMED, "duplicate JSON member")
    return dict(pairs)


def parse_json_object(data: bytes) -> dict:
    try:
        obj = json.loads(data.decode("utf-8"), object_pairs_hook=_no_duplicates)
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise OtaError(MALFORMED, f"invalid JSON: {e}") from None
    if not isinstance(obj, dict):
        raise OtaError(MALFORMED, "JSON value is not an object")
    return obj


def seq_for_version(version: str) -> int:
    m = VERSION_RE.match(version)
    if not m:
        raise OtaError(BAD_SCHEMA, f"version {version!r} is not MAJOR.MINOR.PATCH in range")
    major, minor, patch = (int(x) for x in m.groups())
    return major * 10000 + minor * 100 + patch


def raw_to_der(signature: bytes) -> bytes:
    r = int.from_bytes(signature[:P256_COORD_BYTES], "big")
    s = int.from_bytes(signature[P256_COORD_BYTES:], "big")
    return encode_dss_signature(r, s)


def der_to_raw(der: bytes) -> bytes:
    r, s = decode_dss_signature(der)
    return r.to_bytes(P256_COORD_BYTES, "big") + s.to_bytes(P256_COORD_BYTES, "big")


def load_public_key(pem: bytes) -> ec.EllipticCurvePublicKey:
    key = serialization.load_pem_public_key(pem)
    if not isinstance(key, ec.EllipticCurvePublicKey) or key.curve.name != "secp256r1":
        raise SystemExit("error: public key is not ECDSA P-256")
    return key


def load_private_key(pem: bytes) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(pem, password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise SystemExit("error: signing key is not ECDSA P-256")
    return key


# ---- Signing ----


def build_payload(
    image: bytes, version: str, url: str, board: str, min_protocol: int
) -> dict:
    return {
        "schema": SCHEMA,
        "board": board,
        "version": version,
        "seq": seq_for_version(version),
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "min_protocol": min_protocol,
        "url": url,
    }


def sign_token(header: dict, payload: dict, key: ec.EllipticCurvePrivateKey) -> str:
    signing_input = (
        b64url_encode(json.dumps(header, separators=(",", ":")).encode())
        + "."
        + b64url_encode(json.dumps(payload, separators=(",", ":")).encode())
    )
    der = key.sign(signing_input.encode("ascii"), ec.ECDSA(hashes.SHA256()))
    return signing_input + "." + b64url_encode(der_to_raw(der))


# ---- Verification (the rules in docs/ota/manifest.md, in order) ----


def verify_manifest(token: bytes, ctx: Context) -> dict:
    """Returns the payload, or raises OtaError with the first rule that fails."""
    token = token.rstrip(b"\r\n")
    # 1. Parse.
    if len(token) > MAX_TOKEN_BYTES:
        raise OtaError(MALFORMED, "token too long")
    try:
        text = token.decode("ascii")
    except UnicodeDecodeError:
        raise OtaError(MALFORMED, "token is not ASCII") from None
    parts = text.split(".")
    if len(parts) != 3 or not parts[0] or not parts[1]:
        raise OtaError(MALFORMED, "expected header.payload.signature")
    header = parse_json_object(b64url_decode(parts[0]))
    payload_bytes = b64url_decode(parts[1])
    signature = b64url_decode(parts[2])

    # 2. Header.
    if "crit" in header:
        raise OtaError(MALFORMED, "crit header not supported")
    if header.get("alg") != ALG:
        raise OtaError(UNSUPPORTED_ALG, repr(header.get("alg")))
    if header.get("typ") != TYP:
        raise OtaError(UNSUPPORTED_TYPE, repr(header.get("typ")))
    kid = header.get("kid")
    if not isinstance(kid, str) or kid not in ctx.keys:
        raise OtaError(UNKNOWN_KID, repr(kid))

    # 3. Signature, over the ASCII bytes of "header.payload". Nothing in the payload is
    # trusted before this passes.
    if len(signature) != 2 * P256_COORD_BYTES:
        raise OtaError(BAD_SIGNATURE, "ES256 signatures are 64 bytes")
    try:
        ctx.keys[kid].verify(
            raw_to_der(signature),
            f"{parts[0]}.{parts[1]}".encode("ascii"),
            ec.ECDSA(hashes.SHA256()),
        )
    except (InvalidSignature, ValueError):
        raise OtaError(BAD_SIGNATURE) from None

    # 4. Payload.
    payload = parse_json_object(payload_bytes)
    for name, kind in REQUIRED.items():
        value = payload.get(name)
        if not isinstance(value, kind) or isinstance(value, bool):
            raise OtaError(BAD_SCHEMA, f"{name} missing or not {kind.__name__}")
    if payload["schema"] != SCHEMA:
        raise OtaError(BAD_SCHEMA, f"schema {payload['schema']}")
    if payload["seq"] != seq_for_version(payload["version"]):
        raise OtaError(BAD_SCHEMA, "seq does not match version")
    if not SHA256_RE.match(payload["sha256"]):
        raise OtaError(BAD_SCHEMA, "sha256 is not 64 lowercase hex digits")
    if payload["size"] <= 0 or payload["min_protocol"] < 0:
        raise OtaError(BAD_SCHEMA, "size or min_protocol out of range")
    if payload["board"] != ctx.board:
        raise OtaError(WRONG_BOARD, payload["board"])
    if payload["seq"] <= ctx.running_seq:
        raise OtaError(NOT_NEWER, f"seq {payload['seq']} <= running {ctx.running_seq}")
    if payload["size"] > ctx.slot_size:
        raise OtaError(TOO_LARGE, f"{payload['size']} > slot {ctx.slot_size}")
    if not payload["url"].startswith("https://"):
        raise OtaError(INSECURE_URL, payload["url"])
    return payload


def check_image(image: bytes, payload: dict) -> None:
    """Rule 6: the downloaded image must match the manifest exactly."""
    if len(image) != payload["size"]:
        raise OtaError(SIZE_MISMATCH, f"{len(image)} != {payload['size']}")
    if hashlib.sha256(image).hexdigest() != payload["sha256"]:
        raise OtaError(HASH_MISMATCH)


# ---- Golden vectors ----

VECTOR_BOARD = DEFAULT_BOARD
VECTOR_RUNNING_SEQ = 200  # the device under test runs 0.2.0
VECTOR_SLOT_SIZE = 6291456  # app0 in firmware/partitions.csv
VECTOR_KID = "test-only"
VECTOR_URL = "https://github.com/D4rth-C0d3r/sarathi/releases/download/v0.3.0/test.bin"


def _fixture_image() -> bytes:
    return bytes((i * 31 + 7) % 251 for i in range(4096))


def make_vectors(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.jws"):
        old.unlink()
    image = _fixture_image()
    corrupt = bytearray(image)
    corrupt[1000] ^= 0x01

    # The throwaway key lives only in this process; its private half is never written.
    key = ec.generate_private_key(ec.SECP256R1())
    public_pem = key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
    )

    header = {"alg": ALG, "typ": TYP, "kid": VECTOR_KID}
    good = build_payload(image, "0.3.0", VECTOR_URL, VECTOR_BOARD, 1)
    valid = sign_token(header, good, key)
    h, p, s = valid.split(".")

    def signed(payload: dict | None = None, hdr: dict | None = None) -> str:
        return sign_token(hdr or header, payload if payload is not None else good, key)

    def unsigned(hdr: dict, payload: dict, sig: bytes = b"") -> str:
        return (
            b64url_encode(json.dumps(hdr, separators=(",", ":")).encode())
            + "."
            + b64url_encode(json.dumps(payload, separators=(",", ":")).encode())
            + "."
            + b64url_encode(sig)
        )

    def changed(**fields) -> dict:
        payload = dict(good)
        payload.update(fields)
        return payload

    def signed_raw_payload(raw: bytes) -> str:
        signing_input = b64url_encode(json.dumps(header, separators=(",", ":")).encode())
        signing_input += "." + b64url_encode(raw)
        der = key.sign(signing_input.encode("ascii"), ec.ECDSA(hashes.SHA256()))
        return signing_input + "." + b64url_encode(der_to_raw(der))

    flipped = bytearray(b64url_decode(s))
    flipped[10] ^= 0x01
    hs_header = {"alg": "HS256", "typ": TYP, "kid": VECTOR_KID}
    hs_input = unsigned(hs_header, good).rsplit(".", 1)[0]
    hs_sig = hmac.new(public_pem, hs_input.encode("ascii"), hashlib.sha256).digest()
    no_sha = {k: v for k, v in good.items() if k != "sha256"}
    dup = json.dumps(good, separators=(",", ":"))[:-1] + ',"board":"other"}'

    cases = [
        ("valid", valid, "image.bin", OK, "Signed by the test key; image matches."),
        ("tampered_payload", h + "." + b64url_encode(json.dumps(changed(seq=301)).encode()) + "." + s,
         None, BAD_SIGNATURE, "Payload changed after signing."),
        ("tampered_signature", f"{h}.{p}.{b64url_encode(bytes(flipped))}", None, BAD_SIGNATURE,
         "One bit of the signature flipped."),
        ("alg_none", unsigned(dict(header, alg="none"), good), None, UNSUPPORTED_ALG,
         "Unsigned token claiming alg none."),
        ("alg_hs256", f"{hs_input}.{b64url_encode(hs_sig)}", None, UNSUPPORTED_ALG,
         "HMAC keyed with the public key (algorithm confusion)."),
        ("unknown_kid", signed(hdr=dict(header, kid="someone-else")), None, UNKNOWN_KID,
         "Validly signed, but names a key the device doesn't trust."),
        ("wrong_typ", signed(hdr=dict(header, typ="JWT")), None, UNSUPPORTED_TYPE,
         "A JWS that isn't a sārathi manifest."),
        ("wrong_board", signed(changed(board="other-board")), None, WRONG_BOARD,
         "Signed manifest for a different board."),
        ("not_newer", signed(changed(version="0.2.0", seq=200)), None, NOT_NEWER,
         "Replay of the running version (downgrade protection)."),
        ("too_large", signed(changed(size=VECTOR_SLOT_SIZE + 1)), None, TOO_LARGE,
         "Image larger than the OTA slot."),
        ("insecure_url", signed(changed(url=VECTOR_URL.replace("https", "http", 1))), None,
         INSECURE_URL, "Plain-HTTP download URL."),
        ("seq_mismatch", signed(changed(seq=301)), None, BAD_SCHEMA,
         "seq disagrees with version."),
        ("missing_sha256", signed(no_sha), None, BAD_SCHEMA, "Required field missing."),
        ("schema_2", signed(changed(schema=2)), None, BAD_SCHEMA, "Unknown schema version."),
        ("prerelease_version", signed(changed(version="0.3.0-rc1")), None, BAD_SCHEMA,
         "Pre-release versions are never published as manifests."),
        ("duplicate_member", signed_raw_payload(dup.encode()), None, MALFORMED,
         "Payload JSON with a duplicate member."),
        ("two_parts", f"{h}.{p}", None, MALFORMED, "Not three parts."),
        ("bad_base64", f"{h}.{p}=.{s}", None, MALFORMED, "Padding is not allowed in base64url."),
        ("too_long", signed(changed(url=VECTOR_URL + "?" + "x" * 2048)), None, MALFORMED,
         "Token over 2048 bytes."),
        ("hash_mismatch", valid, "image_corrupt.bin", HASH_MISMATCH,
         "Valid manifest; one bit of the image flipped."),
        ("size_mismatch", valid, "image_truncated.bin", SIZE_MISMATCH,
         "Valid manifest; image truncated."),
    ]

    (out / "image.bin").write_bytes(image)
    (out / "image_corrupt.bin").write_bytes(bytes(corrupt))
    (out / "image_truncated.bin").write_bytes(image[:-16])
    index = {
        "comment": "Golden OTA manifest vectors (docs/ota/manifest.md). Generated by "
        "tools/ota_manifest.py make-vectors with a throwaway key that only signed these "
        "files; its kid 'test-only' is never trusted by firmware.",
        "context": {
            "board": VECTOR_BOARD,
            "running_seq": VECTOR_RUNNING_SEQ,
            "slot_size": VECTOR_SLOT_SIZE,
            "kid": VECTOR_KID,
            "public_key_pem": public_pem.decode("ascii"),
        },
        "cases": [],
    }
    for name, token, image_file, expect, note in cases:
        (out / f"{name}.jws").write_text(token + "\n", encoding="ascii")
        case = {"name": name, "manifest": f"{name}.jws", "expect": expect, "note": note}
        if image_file:
            case["image"] = image_file
        index["cases"].append(case)
    (out / "vectors.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(cases)} vectors to {out}")


def run_case(directory: Path, ctx: Context, case: dict) -> str:
    try:
        payload = verify_manifest((directory / case["manifest"]).read_bytes(), ctx)
        if "image" in case:
            check_image((directory / case["image"]).read_bytes(), payload)
    except OtaError as e:
        return e.code
    return OK


def check_vectors(directory: Path) -> int:
    index = json.loads((directory / "vectors.json").read_text(encoding="utf-8"))
    c = index["context"]
    ctx = Context(
        c["board"],
        c["running_seq"],
        c["slot_size"],
        {c["kid"]: load_public_key(c["public_key_pem"].encode("ascii"))},
    )
    failures = 0
    for case in index["cases"]:
        got = run_case(directory, ctx, case)
        ok = got == case["expect"]
        failures += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {case['name']}: expected {case['expect']}, got {got}")
    print(f"{len(index['cases']) - failures}/{len(index['cases'])} vectors as expected")
    return 1 if failures else 0


# ---- Command line ----


def _read_signing_key(args: argparse.Namespace) -> ec.EllipticCurvePrivateKey:
    if args.key_env:
        pem = os.environ.get(args.key_env, "")
        if not pem:
            raise SystemExit(f"error: environment variable {args.key_env} is empty")
        return load_private_key(pem.encode("ascii"))
    return load_private_key(Path(args.key_file).read_bytes())


def cmd_sign(args: argparse.Namespace) -> int:
    image = args.image.read_bytes()
    payload = build_payload(image, args.version, args.url, args.board, args.min_protocol)
    token = sign_token({"alg": ALG, "typ": TYP, "kid": args.kid}, payload, _read_signing_key(args))
    args.out.write_text(token, encoding="ascii")
    print(json.dumps(payload, indent=2))
    print(f"signed with kid {args.kid}; wrote {args.out}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    ctx = Context(
        args.board, args.running_seq, args.slot_size, {args.kid: load_public_key(args.pubkey.read_bytes())}
    )
    try:
        payload = verify_manifest(args.manifest.read_bytes(), ctx)
        if args.image:
            check_image(args.image.read_bytes(), payload)
    except OtaError as e:
        print(f"rejected: {e}")
        return 1
    print(json.dumps(payload, indent=2))
    print("accepted")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    s = sub.add_parser("sign", help="build and sign a manifest for an image")
    s.add_argument("--image", type=Path, required=True)
    s.add_argument("--version", required=True, help="MAJOR.MINOR.PATCH, without the v")
    s.add_argument("--url", required=True, help="https:// download URL of the image")
    s.add_argument("--kid", required=True)
    keys = s.add_mutually_exclusive_group(required=True)
    keys.add_argument("--key-file", help="PEM private key file")
    keys.add_argument("--key-env", help="environment variable holding the PEM private key")
    s.add_argument("--board", default=DEFAULT_BOARD)
    s.add_argument("--min-protocol", type=int, default=1)
    s.add_argument("--out", type=Path, default=Path("manifest.jws"))
    s.set_defaults(func=cmd_sign)

    v = sub.add_parser("verify", help="check a manifest, and optionally its image")
    v.add_argument("--manifest", type=Path, required=True)
    v.add_argument("--pubkey", type=Path, required=True, help="PEM public key")
    v.add_argument("--kid", required=True, help="kid the public key is trusted under")
    v.add_argument("--image", type=Path)
    v.add_argument("--board", default=DEFAULT_BOARD)
    v.add_argument("--running-seq", type=int, default=0)
    v.add_argument("--slot-size", type=int, default=VECTOR_SLOT_SIZE)
    v.set_defaults(func=cmd_verify)

    m = sub.add_parser("make-vectors", help="regenerate the golden vectors (throwaway key)")
    m.add_argument("dir", type=Path)
    m.set_defaults(func=lambda a: make_vectors(a.dir) or 0)

    c = sub.add_parser("check-vectors", help="check every golden vector")
    c.add_argument("dir", type=Path)
    c.set_defaults(func=lambda a: check_vectors(a.dir))

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
