---
description: "Use frozen Qwen or Ornith Bole experiences in RSI-infer, with exact-model isolation and durable delivery receipts."
kind: "package-reference"
---

# @deepseek-ai/dsh-skill-bole-experience

English | [中文](README.zh.md)

## Summary

Use approved Bole experiences without changing Standard-mode tools, model sampling, or the existing memory stores. RSI-infer selects Qwen service v3 or Ornith V18 by exact model id and supplies the relevant service-lifecycle instructions. Standard mode does not mount this plugin. Other models receive no Bole experience.

## Table of Contents

- [Use this package](#use-this-package)
- [Understand the implementation](#understand-the-implementation)
- [Model Experience](#model-experience)
- [Known Limitations and Deferred Work](#known-limitations-and-deferred-work)
- [Dev Note](#dev-note)

-----

<a id="use-this-package"></a>
## Use this package

Choose RSI-infer for a new task; choose Standard for a no-Bole-experience control. Keep the model, input, permissions and initial workspace identical for a comparison. Ordinary project Skills remain available in both modes.

### Minimal configuration

The shipped [RSI-infer preset](../../bundle/web-app/presets/rsi-infer.patch.yml) includes this row alongside Standard's plugins:

```yaml
- id: bole-experience
  name: '@deepseek-ai/dsh-skill-bole-experience'
  config:
    maxContextBytes: 4096
```

| Field | Default | Meaning |
|---|---|---|
| `maxContextBytes` | `4096` | Positive integer bounding the complete automatic message as UTF-8 JSON, including its framing and receipt; an oversized message stops admission rather than truncating instructions |

[Final assets](assets/final-experiences.json) own the original experience bodies, hashes, accepted model ids, source deployments and historical full-run identities. The automatic route uses exact ids, not model-family substring matching. The active provider/model pair is recorded separately from the historical source deployment.

| Approved model ids | Frozen experience | Historical result |
|---|---|---|
| `qwen3.8-27b`, `Qwen3.8-27B` | Service experience v3, first round | 63/89 (70.79%) |
| `Ornith-1.5-35B-A3B`, `ornith-1.5-35b-a3b` | Ornith V18, highest verified experience run | 66/89 (74.16%) |

### Verify delivery

A durable `user/message` with source kind `bole-experience` records the selected bundle and current route. `status: delivered` and non-empty `deliveredMemoryIds` mean the complete instructions reached that request. `available` means only the Skill catalog and routing notice were offered; a subsequent `skill` tool result proves explicit loading. `unsupported-model` carries no experience. Standard emits none of these receipts.

-----

<a id="understand-the-implementation"></a>
## Understand the implementation

<details>
<summary>Implementation internals — click to expand</summary>

Prompt assembly resolves the current model before the skill catalog is read. Each Agent receives its own provider with the approved skill and non-invocable entries shadowing foreign-model experience inherited from a parent. Direct loading of a foreign entry returns no body. Public requests to launch or retain a service receive the original body once through the existing logged pre-step mechanism. The outbound request listener checks the actual route before admission.

The plugin never reads or writes live Bole memory, promotes candidates, changes budgets other than its own context bound, or adds review/retry policy. [Source selection](src/bundles.ts) validates packaged records and SHA-256 bodies; [the plugin](src/index.ts) owns scoped registration and receipts. Agent disposal and plugin unload remove its providers.

The receipt is qualified producer attribution, not a required replay interpreter. Readers without this plugin preserve its JSON metadata and derive the recorded user content; only this producer uses receipts for duplicate and route checks. Full-host integration must acknowledge the additive source-kind change through the host's persistence-history generator; it does not require a Session format bump.

**Runtime invariant:** No companion is published. The plugin has no independently maintained state projection; the existing skill registry and durable session messages own its registrations and delivered text.

</details>

-----

<a id="model-experience"></a>
## Model Experience

### Model-specific final experience

#### What the model sees

Only the approved model's skill summary is model-invocable. A matching public service-lifecycle request receives the complete body in a `<bole-experience>` block. An unrelated request receives a short availability notice, not the body. All delivered text and the receipt are present in the session log.

#### Token effect

RSI-infer adds one skill summary and one initial notice or instruction block. A later matching request may add the body once; repeated turns do not redeliver it. Standard adds no Bole text.

#### KV Cache effect

Experience is appended as logged user context; the Standard system prompt and tool schemas remain unchanged. Model-specific catalogs and context can change request-prefix reuse; provider cache behavior is not guaranteed.

## Known Limitations and Deferred Work

<a id="known-limitations-and-deferred-work"></a>

The final experiences are frozen instructions, not a complete replay of the historical benchmark runtime.

- Historical Terminal-Bench scores do not establish Deep SWE gains or gains under this repository's deployment.
- Provider/model ids identify the configured route, not independently verified backend weights. After its first request, an RSI-infer session cannot change routes, even after compaction removes a receipt; start a new task. Forked history carrying another route's receipt is refused.
- This plugin uses conservative English/Chinese public-task matching, not a semantic classifier. Matching may miss a request; the approved skill remains directly loadable.
- Only the approved final service experiences are bundled. Rejected, candidate and retired experiences, hidden answers, credentials and benchmark-specific patches are excluded.
- This repository is a packages subtree. Its full Harness host must add this package to `tsconfig.host.json` and its source alias to `tsconfig.base.json` before the normal workspace build; the package's own tsconfig follows the existing three-level host layout.

<a id="dev-note"></a>
### Dev Note

<details>
<summary>Working context for maintainers — click to expand</summary>

None.

</details>
