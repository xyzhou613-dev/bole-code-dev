---
description: "RSI-infer handoff: mode comparisons, final experience sources, full-host integration, local preview, and acceptance limits."
---

# RSI-infer development and deployment handoff

English | [中文](RSI-INFER-HANDOFF.zh.md)

## Summary

Maintainers can compare final experience delivery in Standard and RSI-infer under the same model and task conditions. Branch `feat/rsi-infer-experience-mode` is developed from the user's `bole-dsh-packages` remote, rather than pushing another Bole development branch into it. Experience is read-only and isolated by exact model ID; historical scores do not establish gains from this integration.

## Table of Contents

- [Modes and assets](#modes-and-assets)
- [Full-host integration](#host-integration)
- [Delivery and isolation](#delivery-and-isolation)
- [Verification record](#verification)
- [Local preview](#local-preview)
- [Acceptance and rollback](#acceptance-and-rollback)
- [Dev Note](#dev-note)

-----

<a id="modes-and-assets"></a>
## Modes and assets

Standard is the control without Bole experience; RSI-infer retains the same Standard plugin composition and adds only the experience plugin. Ordinary project Skills remain in both, so “without experience” does not mean disabling all Skills. The four original preset patches are unchanged; this does not change the agent loop, temperature, retries, tool budgets, or existing memory stores, nor run online self-modification, review, or candidate promotion.

| Display name | Preset ID | Mounts Bole experience |
|---|---|---|
| Standard | `standard` | No |
| PTC | `ptc` | No |
| Minimal | `minimal` | No |
| Creator | `cordis` | No |
| RSI-infer | `rsi-infer` | Yes |

The [frozen asset](../../skill/skill-bole-experience/assets/final-experiences.json) is the single data source for bodies, allowed models, source deployments, and historical run identities. Body changes require matching SHA-256, owning snapshots, and revalidation; new candidates must not be inserted directly as final experience.

| Exact model IDs | Experience bundle | Verified source score |
|---|---|---|
| `qwen3.8-27b`, `Qwen3.8-27B` | `qwen38-service-v3`, service experience v3 first round | Terminal-Bench 2.1: 63/89, 70.79% |
| `Ornith-1.5-35B-A3B`, `ornith-1.5-35b-a3b` | `ornith-v18`, highest verified Ornith experience run | Terminal-Bench 2.1: 66/89, 74.16% |

Both experiences address service lifetimes after Agent exit and external verification, not every code-repair task or a complete historical runtime. These source scores are not repository retest results and cannot establish Deep SWE gains; this integration has not run a new full benchmark.

-----

<a id="host-integration"></a>
## Full-host integration

This repository root corresponds to a complete DeepSeek Harness's `packages/`, not an application root that can be installed and launched directly. The host also needs `apps/`, `vendor/`, `native/`, build scripts, and root TypeScript settings. The built local host uses official `dsh-v0.1.7-alpha.2`, overlays this repository's package files, and retains upstream packages absent here.

Host integrators place this branch's files at matching host `packages/` paths, retain other host content, and add the root settings below. The plugin's own and Web bundle tsconfigs are in this branch; do not move sources or weaken type checks to fit the packages-only checkout.

Add this entry to the existing host `tsconfig.base.json` `compilerOptions.paths`, retaining other aliases:

```json
{
  "@deepseek-ai/dsh-skill-bole-experience": ["./packages/skill/skill-bole-experience/src/index.ts"]
}
```

Add this entry to the existing host `tsconfig.host.json` `references` array, retaining other references:

```json
{ "path": "./packages/skill/skill-bole-experience" }
```

Then use the full host's normal installation, build, and `dsh` profile workflow; the local full build completed. The new package is a Cordis function plugin, not a standalone application or an independent bundle with `dsh.bundle.patch`; the Web bundle mounts it. Publication must include `assets/`, otherwise startup validation fails.

The additive receipt kind also needs a same-version acknowledgement in the full host's persistence history. The local validation host generated and verified this record without changing format 4 or accepted checkpoints. This packages-only repository cannot publish the host's root `docs/` history; integrators generate it against their actual host using the supplied [bilingual prose](rsi-infer-persistence-prose.json):

```sh
pnpm run persistence-changes --record 2026-09-28-bole-experience-attribution --decision same-version --prose packages/bundle/web-app/rsi-infer-persistence-prose.json
pnpm run verify-persistence-changes
pnpm run verify-persistence-formats --write
pnpm run verify-persistence-formats
```

The generator owns the paired acknowledgement, schema snapshot and current persistence catalogs. Commit those outputs in the full-host repository, not under `packages/docs/`. If that record already exists, verify it rather than overwrite it; unrelated host changes need their own reviewed acknowledgement. Do not weaken the checker or bump the Session format to avoid recording a compatible addition. Regenerate other host-owned catalogs required by `doc-sync` after integration.

| Maintainer entry point | Responsibility |
|---|---|
| [Preset patch](presets/rsi-infer.patch.yml) and [Web manifest](package.json) | Standard capabilities plus experience, dependencies, and patch order |
| [Experience reference](../../skill/skill-bole-experience/README.md) | Configuration, model-visible content, and durable limitations |
| [Asset validation](../../skill/skill-bole-experience/src/bundles.ts) and [plugin](../../skill/skill-bole-experience/src/index.ts) | Body hashes, public-task matching, Agent scope, route checks, and disposal |
| [Display mapping](../../preset/agent-preset-registry/src/display.ts) and [UI copy](../../client/ui-agent-preset/src/client/locales.ts) | The fifth mode's display and explanations |

-----

<a id="delivery-and-isolation"></a>
## Delivery and isolation

A mode label does not establish body delivery. Inspect durable `user/message` records with `source.kind: bole-experience`, alongside actual `skill` tool results, to determine whether a body was loaded.

| Status | Meaning |
|---|---|
| `delivered` | The complete body was added automatically; `deliveredMemoryIds` is non-empty, with current provider/model, bundle ID, and body hash checked |
| `available` | Only the model-specific Skill catalog and short notice, not body delivery; explicit loading needs a `skill` result |
| `unsupported-model` | The ID is outside the allowed list and receives no Bole body |
| Standard | This plugin is not mounted; neither its receipts nor final Bole Skill should appear |

Public English or Chinese tasks explicitly starting, deploying, or retaining a service can trigger automatic body delivery; generic code questions may not match, but the owning model's Skill can be loaded explicitly. Matching does not read hidden tests, gold answers, or verifier answers. The complete automatic UTF-8 JSON message defaults to 4096 bytes including framing and receipt; overflow blocks inference rather than truncating the body.

Each Agent independently registers its model's Skill and rejects direct loading of another model's experience. Exact provider/model is bound after first inference; changing models requires a new task, compaction does not remove the binding, and forked history containing another route's experience is rejected. IDs verify configured routes only: operations must confirm backend weights rather than disguise another model under an allowed ID.

-----

<a id="verification"></a>
## Verification record

These are observed local evidence and gaps dated 2026-09-28, not complete CI or benchmark acceptance. The current delivery report owns the new handoff document's pre-commit check results.

| Scope | Result |
|---|---|
| Five focused test files | 56 passed: experience plugin 21, preset inventory 1, display mapping 4, UI-related tests 30 |
| Isolation and model-visible tests | Covers real Loader, durable-message snapshots, cross-model direct-call denial, route switches, post-compaction binding, concurrent Agents, disposal, and complete-byte bounds |
| Final affected rerun | 64 tests passed across the plugin, preset inventory and two adjacent persistence-source files; includes JSON receipt restoration after plugin unload |
| Types and build | Relevant leaf typechecks, full-host build, and new-plugin built-asset smoke passed; relevant dependency, JSDoc, invariant, and README checks passed |
| Pre-commit checks | Full-host contract-ready lint passed; changed README pairs and plugin-package catalog passed. Broad `doc-sync` initially passed 29 gates and failed 13, including stale host-generated catalogs and Windows symlink EPERM; it is not claimed green |
| Persistence and documentation follow-up | Same-version history verification passed. Quick documentation checks passed 19 gates; the remaining stale format-reference gate passed individually after regeneration. No full `doc-sync` success is asserted |
| Local Web | Authenticated HTTP 200, real page displayed RSI-infer, listening only on `127.0.0.1:4317` |
| Full GUI unit lane | 14 failed, 7486 passed; includes existing brand/endpoint differences from upstream expectations, Windows symlink EPERM, CRLF, and connection tests; not claimed green |
| Full Web replay lane | 89 failed, 22 passed, 410 skipped; most scenarios lack Playwright Chromium, with additional fixture/connection issues; not claimed green |
| Real models and benchmarks | Qwen/Ornith live requests are unverified; a preview message hit `MISSING_CREDENTIAL` on the default DeepSeek route, not an experience-effect measurement |

Behavior evidence is owned by [experience tests](../../skill/skill-bole-experience/tests/), the [preset inventory test](tests/rsi-infer-preset.spec.ts), and the display/UI packages' tests. Do not bulk-update unrelated brand snapshots, overwrite other branches' experience, or disable checks to turn repository-wide checks green.

-----

<a id="local-preview"></a>
## Local preview

This machine's preview is in `work/rsi-infer-local-20260928/` under the current task directory, using adjacent full host `work/harness-host-20260928/`. Isolated `state/` owns settings and sessions, `workspace-root/` owns workspaces, and `logs/` owns logs without overwriting the default Harness home. These directories, build outputs, sessions, startup tokens, and machine-local scripts are not part of the push.

| Machine-local script | Purpose and verification |
|---|---|
| `start-preview.ps1` | Starts isolated profile `rsi-local` in the background; startup and duplicate-start detection were verified |
| `open-preview.ps1` | Reads the authenticated URL from startup logs and opens the default browser; syntax checked |
| `stop-preview.ps1` | Stops only processes matching this CLI path and `rsi-local`, preserving data; syntax checked without stopping the service in use for acceptance |

First access uses the host's printed token URL or local open script; after obtaining the signed cookie, the address becomes `http://127.0.0.1:4317/`. A new browser may receive 401 for the bare address: authentication is required, not broken deployment. Do not copy tokens, API keys, private keys, or passwords into the repository or handoff.

Operations must configure a compatible protocol, endpoint, credential reference, and exact model ID in Settings → Models. The existing Qwen GPU 4–7 service can be connected under authorization, but this preview has not established that connection and no Ornith endpoint is configured. Old credentials were not copied, so the default DeepSeek missing-key error is not an RSI-infer effect result.

Pre-push source changes fixed provider teardown and qualified the receipt attribution. The running preview was not interrupted; rebuild and restart it through its local scripts before testing the final source against real models.

-----

<a id="acceptance-and-rollback"></a>
## Acceptance and rollback

Create two fresh sessions and identical initial workspaces for comparison, without carrying changed code, old experience history, or different sampling settings into the other arm. The receiving maintainer performs real-model acceptance after configuring endpoints.

1. Confirm all four original modes and RSI-infer are selectable, with Standard still the default.
2. Compare Standard and RSI-infer on a public service task with an exact Qwen ID: no Bole receipt in Standard; the experience arm records the Qwen bundle and non-empty delivery ID without Ornith content.
3. Repeat with an exact Ornith ID: only the Ornith bundle without Qwen content; check another model receives no experience.
4. Use a task unrelated to service lifetimes to check `available` and explicit Skill loading; do not count a catalog notice as body delivery.
5. Confirm provider/model changes after first inference are rejected before the network request, while a new task can use another model.
6. Compare quality, functional tests, elapsed time, and exceptions; source scores identify history, and one task or a small sample cannot establish overall gains.

To disable experience, create a Standard task without deleting assets or clearing memory stores. Code rollback uses the remote version preceding this feature without deleting other branches or overwriting original experience. Existing RSI-infer logs retain their records and cannot act as a fresh experience-free control.

<a id="dev-note"></a>
## Dev Note

This section records handoff context only, not authority over package READMEs, source, or subsequent CI. The handoff date is 2026-09-28; next steps are connecting real models, completing isolation acceptance, and fixing the full host's browser-test environment before comparable evaluation. This delivery does not push machine-local state or operate the original server checkout or evaluation GPUs.
