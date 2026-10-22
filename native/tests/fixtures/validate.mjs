// Validate captured wire fixtures against 03-events.md and manifest.json.
//
// A fixture is one wire notification per line (`bole.event` / `assistant.delta`
// / `rsi.state`). The manifest records the expected event-type subsequence, the
// terminal outcome, and the capture metadata. This validator checks the shape
// and ordering facts the C library relies on; it does not require a real
// runtime, so it also runs in CI without a model gateway.
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const manifest = JSON.parse(readFileSync(join(here, 'manifest.json'), 'utf8'))
const KNOWN_TYPES = new Set([1, 2, 3, 10, 11, 12, 13, 14, 15, 20, 21, 22, 30, 31, 40, 50, 51])
const DELTA_CHANNELS = new Set(['text', 'reasoning'])
const entries = Array.isArray(manifest.fixtures)
  ? manifest.fixtures
  : Object.entries(manifest).map(([name, entry]) => ({ ...entry, file: entry.file ?? `${name}.jsonl` }))

let failures = 0
const check = (name, ok, what) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}: ${what}`)
  if (!ok) failures++
}

/** True when `wanted` appears in `actual` in order (not necessarily contiguous). */
const isSubsequence = (wanted, actual) => {
  let cursor = 0
  for (const type of actual) {
    if (type === wanted[cursor]) cursor++
    if (cursor === wanted.length) return true
  }
  return wanted.length === 0
}

for (const entry of entries) {
  const name = entry.file.replace(/\.jsonl$/, '')
  let frames
  try {
    frames = readFileSync(join(here, entry.file), 'utf8')
      .split('\n')
      .filter((line) => line.trim() !== '')
      .map((line) => JSON.parse(line))
  } catch (error) {
    check(name, false, `read/parse ${entry.file}: ${error.message}`)
    continue
  }

  const events = frames
    .filter((frame) => frame.method === 'bole.event')
    .map((frame) => frame.params?.event)
    .filter(Boolean)
  const deltas = frames.filter((frame) => frame.method === 'assistant.delta')
  const rsiStates = frames.filter((frame) => frame.method === 'rsi.state')

  check(name, events.length > 0, 'at least one bole.event')
  check(name, frames.every((frame) => frame.jsonrpc === '2.0' && typeof frame.method === 'string'),
    'every line is a JSON-RPC notification')

  const types = events.map((event) => event.type)
  check(name, types.every((type) => KNOWN_TYPES.has(type)), 'all event types known')
  check(name, events.every((event) => event.payload?.v === 1), 'payload.v === 1')
  // 03-events.md §1: runtime-level events carry an empty session_id; the
  // default RSI state is the one such event a fixture can contain today.
  check(name, events.every((event) => typeof event.session_id === 'string'
    && (event.session_id !== '' || (event.type === 15 && event.payload?.scope === 'default'))),
  'session id preserved')
  check(name, events.every((event) => event.session_id === event.payload?.sessionId
    || event.payload?.sessionId === undefined), 'payload session id agrees')
  check(name, events.every((event, index) => index === 0 || event.event_id > events[index - 1].event_id),
    'event ids strictly increase')
  check(name, events.every((event, index) => index === 0 || event.ts_ms >= events[index - 1].ts_ms),
    'timestamps do not go backwards')

  if (Array.isArray(entry.expectedEventTypes) && entry.expectedEventTypes.length > 0) {
    check(name, isSubsequence(entry.expectedEventTypes, types),
      `expected type subsequence ${entry.expectedEventTypes.join(',')}`)
  }

  const terminals = events.filter((event) => event.type === 50 || event.type === 51)
  check(name, terminals.length === 1, 'exactly one terminal event')
  const terminal = terminals[0]
  if (terminal) {
    if (entry.terminal === 'failed') {
      check(name, terminal.type === 51, 'terminal TASK_FAILED')
      check(name, typeof terminal.payload?.code === 'string' && terminal.payload.code !== '',
        'failure code present')
    } else {
      check(name, terminal.type === 50, `terminal TASK_FINISHED (${entry.terminal})`)
      check(name, terminal.payload?.outcome === entry.terminal,
        `terminal outcome ${entry.terminal}`)
    }
  }

  check(name, deltas.every((frame) => DELTA_CHANNELS.has(frame.params?.channel)),
    'delta channel is text/reasoning')
  check(name, deltas.every((frame) => typeof frame.params?.text === 'string'),
    'delta text is a string')
  check(name, rsiStates.every((frame) => typeof frame.params?.scope === 'string'),
    'rsi.state has a scope')
}

console.log(failures === 0 ? 'ALL FIXTURES PASS' : `${failures} FAILURE(S)`)
process.exit(failures === 0 ? 0 : 1)
