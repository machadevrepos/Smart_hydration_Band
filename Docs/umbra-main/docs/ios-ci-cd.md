# iOS CI/CD — Umbra

Status as of this writing: **CI is live and enforced. TestFlight deployment
is wired but blocked** on Apple Distribution signing material the client
has not yet provided. Nothing in this document should be read as "TestFlight
upload is working" until someone has actually run the deploy workflow
successfully — see "What still requires client-provided credentials" at the
bottom.

## 1. Architecture

```
Git push / PR                          workflow_dispatch (manual)
      │                                          │
      ▼                                          ▼
.github/workflows/ci.yml          .github/workflows/ios-testflight.yml
      │                                          │
      ├─ flutter pub get                         ├─ flutter pub get
      ├─ flutter analyze                         ├─ verify required secrets present
      ├─ flutter test                            ├─ fastlane ios beta:
      └─ flutter build ios                       │    ├─ import Distribution cert + profile
           --no-codesign                         │    │  into a throwaway CI keychain
           (proves the app builds,                    ├─ authenticate to App Store Connect
            no signing required)                      │  via API key (no Apple ID needed)
                                                        ├─ increment_build_number
                                                        ├─ build_app (gym) → signed .ipa
                                                        ├─ upload_to_testflight
                                                        └─ delete the throwaway keychain
```

Two separate workflows, on purpose:

- **`ci.yml`** — runs on every PR and every push to `main`. Never touches
  Apple signing, never uploads anywhere. Its job is to catch analyzer/test/
  build regressions fast, on a fully deterministic macOS runner. This is
  safe to leave fully automatic.
- **`ios-testflight.yml`** — `workflow_dispatch` only, with a required
  "type deploy to confirm" input. Nothing about this workflow runs
  automatically. Promote it to an automatic trigger (tags, a release
  branch, whatever the team decides) only as its own deliberate change,
  after the manual flow has been proven to actually produce and upload a
  signed build.

## 2. Required GitHub Secrets

None of these exist in the repo yet — they need to be added under
**Settings → Secrets and variables → Actions** before `ios-testflight.yml`
can do anything beyond fail its own secret-presence check.

| Secret | Contents | Source |
|---|---|---|
| `ASC_KEY_ID` | `KPZ62YTB2P` | Already known — App Store Connect API key ID |
| `ASC_ISSUER_ID` | `f7f80ab9-67a2-4f83-9152-460f8d0405b1` | Already known — ASC Issuer ID |
| `ASC_API_KEY_CONTENT` | Full contents of the `.p8` file, pasted as-is (including `-----BEGIN/END PRIVATE KEY-----` lines) | The local `.p8` — never printed or committed, paste it directly into the GitHub Secret form |
| `IOS_DIST_CERTIFICATE_P12_BASE64` | The Apple Distribution `.p12`, base64-encoded (`base64 -i cert.p12 \| pbcopy`) | Client-provided, pending |
| `IOS_DIST_CERTIFICATE_PASSWORD` | Password protecting that `.p12` | Client-provided, pending |
| `IOS_DIST_PROVISIONING_PROFILE_BASE64` | The distribution `.mobileprovision` for `com.umbrawellness.umbra`, base64-encoded | Client-provided, pending |
| `IOS_KEYCHAIN_PASSWORD` | Any strong random string — only used to lock/unlock the throwaway CI keychain for the duration of one run | Generate one yourself, e.g. `openssl rand -base64 32`; it's not an Apple credential |

Base64-encode a binary file for pasting into a GitHub Secret with:

```
base64 -i /path/to/file.p12 | pbcopy
```

## 3. Apple signing requirements

- **Bundle ID:** `com.umbrawellness.umbra`
- **Team ID:** `8764YSZYXR`
- **Signing style:** the project uses `CODE_SIGN_STYLE = Automatic` locally,
  but CI signs manually (direct cert + profile import — see §6) because
  automatic signing needs an authenticated Xcode session, which a CI
  runner doesn't have.
- The provisioning profile must be a **Distribution** (App Store) profile
  for exactly `com.umbrawellness.umbra`, matching the Distribution
  certificate's private key.
- `ExportOptions.plist` at the repo root (method `app-store-connect`, team
  `8764YSZYXR`, automatic signing style) already exists from the prior
  manual TestFlight upload and is reused as-is by `fastlane build_app` — no
  need to regenerate it.

## 4. App Store Connect API requirements

Authentication uses the App Store Connect **API key**, not an Apple ID —
deliberately, since we don't have the Umbra Apple ID/password. The Fastfile
passes `ASC_KEY_ID` / `ASC_ISSUER_ID` / `ASC_API_KEY_CONTENT` straight into
fastlane's `app_store_connect_api_key` action, which every later fastlane
action (`build_app`'s implicit signing lookups, `upload_to_testflight`)
then reuses. This key never touches disk in the workflow — it's held in
memory by the fastlane process for the run's duration, sourced from a
secret env var.

## 5. How to run the TestFlight workflow

Once the secrets in §2 are all set:

1. GitHub → **Actions** tab → **iOS TestFlight Deploy** → **Run workflow**.
2. In the `confirm` field, type `deploy` exactly. Any other value (or
   leaving it blank) causes the job to no-op via its `if:` guard.
3. Watch the run. `Verify required secrets are present` fails fast with the
   exact missing secret names if anything is absent — no need to dig
   through fastlane output to find that out.
4. On success, the build appears in App Store Connect → TestFlight,
   typically within a few minutes of processing.

## 6. How build numbers are generated

**Do not hand-edit `pubspec.yaml`'s build number for CI releases.** The
workflow computes it instead:

```
BUILD_NUMBER = github.run_number + BUILD_NUMBER_OFFSET
```

- `github.run_number` is GitHub's own per-workflow run counter — starts at
  1 for this workflow's first run, increments by 1 every run, and never
  resets or goes backward (re-running a failed run does *not* reuse the
  old number for a fresh attempt at the *next* run).
- `BUILD_NUMBER_OFFSET` (currently `100`, set at the top of
  `ios-testflight.yml`) exists purely to stay clear of build numbers
  already uploaded manually — the repo's `pubspec.yaml` was last at
  `1.0.0+2` from a manual upload, so anything CI produces needs to be
  higher than whatever's already in App Store Connect for version `1.0.0`.
- `fastlane`'s `increment_build_number` action writes this value straight
  into the Xcode project (`CFBundleVersion`) right before `build_app` runs,
  so `pubspec.yaml` itself is never touched by CI.

**If TestFlight ever rejects an upload for a duplicate/non-increasing
build number:** bump `BUILD_NUMBER_OFFSET` in `ios-testflight.yml` above
the highest build number currently in App Store Connect for the current
version string, and re-run.

**If the marketing version (`1.0.0`) changes:** update it in `pubspec.yaml`
as normal (`version: 1.1.0+2`) — that part of Flutter's versioning is
unaffected by any of the above, only the build-number half is CI-owned.

## 7. How to rotate credentials

- **App Store Connect API key:** revoke the old key in App Store Connect →
  Users and Access → Integrations, generate a new one, update all three
  `ASC_*` GitHub Secrets. No code changes needed.
- **Distribution certificate:** once it's due for renewal (see §8),
  generate a new one, re-export as `.p12`, base64-encode, replace
  `IOS_DIST_CERTIFICATE_P12_BASE64` and `IOS_DIST_CERTIFICATE_PASSWORD`.
  The old certificate's provisioning profile becomes invalid the moment the
  certificate is revoked — replace the profile secret at the same time,
  not after.
- **Provisioning profile:** replace `IOS_DIST_PROVISIONING_PROFILE_BASE64`
  whenever the profile is regenerated (new cert, new entitlement, or
  simply nearing its own expiry — profiles expire yearly, independent of
  the certificate).
- **Keychain password:** cosmetic — rotate any time by generating a new
  random string and updating `IOS_KEYCHAIN_PASSWORD`; it protects nothing
  once the run's keychain is deleted at the end of the job.

## 8. What to do when signing certificates expire

Apple Distribution certificates are valid for one year; provisioning
profiles typically track the certificate's validity. When `fastlane`
starts failing at `import_certificate` or `build_app` with a signing
error:

1. Confirm it's actually expiry, not a secret typo: check the certificate's
   expiry date in Apple Developer → Certificates.
2. Client generates (or authorizes generating) a renewed Distribution
   certificate and a matching renewed provisioning profile for
   `com.umbrawellness.umbra`.
3. Rotate both secrets per §7.
4. Re-run the workflow.

There is no automatic renewal here by design — a signing certificate is
sensitive enough that a human should be the one deciding when to reissue
it, not a scheduled job.

## 9. What still requires client-provided credentials

**Blocked right now:**

- The Apple Distribution certificate + private key (`.p12` + password).
- The Distribution provisioning profile for `com.umbrawellness.umbra`.

Until both are supplied and added as the `IOS_DIST_*` secrets in §2,
`ios-testflight.yml` will stop at its own "Verify required secrets are
present" guard step with a clear error — it will not attempt a build, and
it will not attempt to fabricate or bypass signing.

**Already working, no further input needed:**

- `ci.yml` — analyze, test, and unsigned iOS build validation on every PR
  and push to `main`.
- App Store Connect API authentication (key ID, issuer ID, and the local
  `.p8` are all already available — just needs pasting into
  `ASC_API_KEY_CONTENT`).
- Build-number strategy (§6) — fully automated, no client input needed.
- Fastlane lane structure (`fastlane/Fastfile`) — complete and
  syntax-validated; it will run as soon as the two blocked secrets above
  exist.

**Do not claim TestFlight deployment is "done"** until someone has actually
triggered `ios-testflight.yml` with real secrets in place and watched a
build land in App Store Connect. Everything described above as "ready" is
ready in the sense of *structurally complete and validated*, not
*proven end to end* — those are different claims and this document is
deliberately careful to keep them separate.
