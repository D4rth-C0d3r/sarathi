# OTA signing keys (public halves)

Public keys the firmware trusts for OTA manifests (ADR-0004). Each private key exists only
as the GitHub Actions secret `OTA_SIGNING_KEY` in the protected `release` environment
(deployments limited to `v*` tags). Private keys are never committed, printed or kept
locally.

| kid | File | Algorithm | Created | Fingerprint (SHA-256 of the DER public key) | Status |
|---|---|---|---|---|---|
| `sarathi-ota-1` | `sarathi-ota-1.pub.pem` | ES256 (ECDSA P-256, SHA-256) | 2026-10-22 | `2a9c6bd0988590d3b2d9264f8877bd5b1e843e5593fde61c77ec64e65debd4c7` | current |

Check a fingerprint:

```bash
openssl pkey -pubin -in docs/ota/keys/sarathi-ota-1.pub.pem -outform DER | sha256sum
```

**Rotating a key:**

1. Generate the new key straight into a second secret, `OTA_SIGNING_KEY_NEXT`, in the same
   environment, the same way as the first. Commit its public key here as `sarathi-ota-2`.
2. Ship a release whose firmware trusts both keys. It is still signed with the old key.
3. Point the release workflow at the new secret and `kid`, then delete the old secret.

If a private key is lost, devices that trust only that key need one USB flash.
