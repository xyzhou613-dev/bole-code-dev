/**
 * Turn one raw capture into a fixture JSONL plus a manifest entry.
 *
 * Fixtures contain only wire notifications the C library consumes
 * (`bole.event` / `assistant.delta` / `rsi.state`), with stable event ids and
 * timestamps. The event shape and order are preserved.
 *
 * Usage:
 *   node runtime/dev/fixtures.mjs --from .dev/tool-ok.jsonl \
 *     --scenario happy-tool --terminal completed --types 10,14,22,30,31,50
 */

import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, isAbsolute, join, resolve } from 'node:path'
import { repoRoot, devRoot } from './shared/runtime.mjs'

const args = process.argv.slice(2)
const valueOf = (name, fallback) => {
  const index = args.indexOf(name)
  return index === -1 ? fallback : args[index + 1]
}

const fromArg = valueOf('--from', '.dev/tool-ok.jsonl')
const from = isAbsolute(fromArg) ? fromArg : resolve(devRoot, fromArg)
const scenario = valueOf('--scenario', 'happy-tool')
const terminal = valueOf('--terminal', 'completed')
const description = valueOf('--description', 'Captured SDK event stream')
const requestedTypes = valueOf('--types', '').split(',').filter(Boolean).map(Number)
const output = resolve(repoRoot, 'native/tests/fixtures', `${scenario}.jsonl`)
const manifestPath = resolve(repoRoot, 'native/tests/fixtures/manifest.json')

const lines = (await readFile(from, 'utf8')).split('\n').filter(Boolean)
const frames = []
for (const line of lines) {
  let frame
  try {
    frame = JSON.parse(line)
  } catch {
    continue
  }
  if (frame.method !== 'bole.event' && frame.method !== 'assistant.delta' && frame.method !== 'rsi.state') continue
  frames.push(frame)
}
if (frames.length === 0) throw new Error(`no SDK notifications in ${from}`)
const types = requestedTypes.length > 0
  ? requestedTypes
  : [...new Set(frames
      .filter((frame) => frame.method === 'bole.event')
      .map((frame) => frame.params.event.type))]
for (let index = 0; index < frames.length; index += 1) {
  const frame = frames[index]
  if (frame.method === 'bole.event') {
    frame.params.event.event_id = index + 1
    frame.params.event.ts_ms = 1_761_100_000_000 + index * 10
  }
}

await mkdir(dirname(output), { recursive: true })
await writeFile(output, `${frames.map((frame) => JSON.stringify(frame)).join('\n')}\n`)

let manifest = { version: 1, fixtures: [] }
try {
  manifest = JSON.parse(await readFile(manifestPath, 'utf8'))
} catch {
  // First fixture: create the manifest below.
}
const entry = {
  file: `${scenario}.jsonl`,
  description,
  capturedAt: new Date().toISOString().slice(0, 10),
  runtime: '0.1.7-alpha.2',
  model: 'mock openai-completions (bole)',
  expectedEventTypes: types,
  terminal,
  timeoutMs: 60_000,
}
manifest.fixtures = [...(manifest.fixtures ?? []).filter((item) => item.file !== entry.file), entry]
  .sort((left, right) => left.file.localeCompare(right.file))
await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`)
console.log(`wrote ${frames.length} frame(s) -> ${output}`)
