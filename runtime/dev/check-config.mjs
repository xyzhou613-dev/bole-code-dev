/**
 * A6 configuration self-check: compose the sdk profile and assert the delivery
 * patch facts the runtime depends on (no duplicate host rows, preset plane,
 * approval, external-network rows, model route).
 *
 * Usage: node runtime/dev/check-config.mjs
 */

import { spawnSync } from 'node:child_process'
import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { cliBin, devPatchFile, ensureDevLinks, repoRoot } from './shared/runtime.mjs'

await ensureDevLinks()
const home = await mkdtemp(join(tmpdir(), 'bole-config-'))
let failures = 0

function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${ok || detail === '' ? '' : `: ${detail}`}`)
  if (!ok) failures += 1
}

/** Return the top-level row block for one id. */
function blockOf(text, id) {
  const lines = text.split('\n')
  const start = lines.findIndex((line) => line === `- id: ${id}`)
  if (start === -1) return undefined
  let end = lines.length
  for (let index = start + 1; index < lines.length; index += 1) {
    if (lines[index].startsWith('- id: ')) { end = index; break }
  }
  return lines.slice(start, end).join('\n')
}

try {
  const result = spawnSync(process.execPath, [cliBin, '--profile', 'sdk', '--patch', devPatchFile, '--dump-config'], {
    cwd: repoRoot,
    env: { ...process.env, DSH_HOME: home, DSH_TELEMETRY_DISABLED: '1' },
    encoding: 'utf8',
  })
  check('dump-config exits 0', result.status === 0, result.stderr?.slice(-500))
  const dump = result.stdout ?? ''

  const topIds = [...dump.matchAll(/^- id: (.+)$/gm)].map((match) => match[1].trim())
  const duplicates = topIds.filter((id, index) => topIds.indexOf(id) !== index)
  check('no duplicate top-level rows', duplicates.length === 0, [...new Set(duplicates)].join(', '))

  check('official SDK server disabled', blockOf(dump, 'sdk-jsonrpc-server')?.includes('disabled: true') === true)
  check('preset registry present', blockOf(dump, 'agent-preset-registry')?.includes('default: standard') === true)
  check('standard preset declared', blockOf(dump, 'preset-standard') !== undefined)
  check('rsi-infer preset declared', blockOf(dump, 'preset-rsi-infer') !== undefined)
  check('subagent settings host row present', blockOf(dump, 'subagent-model-selection-settings') !== undefined)
  check('approval policy never', blockOf(dump, 'approval')?.includes('policy: never') === true)
  check('model route present', blockOf(dump, 'agent-default-model')?.includes('qwen3.8-27b') === true)
  check('bole provider configured', blockOf(dump, 'llm-pi-ai')?.includes('apiKeyEnv: BOLE_API_KEY') === true)

  for (const id of ['web', 'web-search-deepseek', 'web-fetch-http', 'tool-web', 'session-telemetry-otel']) {
    check(`${id} disabled`, blockOf(dump, id)?.includes('disabled: true') === true)
  }
} finally {
  await rm(home, { recursive: true, force: true })
}

if (failures > 0) {
  console.error(`${failures} configuration check(s) failed`)
  process.exitCode = 1
} else {
  console.log('ALL CONFIG CHECKS PASS')
}
