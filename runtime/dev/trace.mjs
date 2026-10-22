/**
 * A5 trace smoke: live and cold export, turn filtering, and write failures.
 *
 * Usage: node runtime/dev/trace.mjs
 */

import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import {
  assert,
  cleanup,
  devPatchFile,
  devRoot,
  ensureDevLinks,
  prepareHome,
  repoRoot,
  RpcClient,
  spawnRuntime,
} from './shared/runtime.mjs'
import { startMockModel } from './mock-model.mjs'

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

async function waitEvent(client, sessionId, type, from = 0, timeoutMs = 20_000) {
  const deadline = Date.now() + timeoutMs
  for (;;) {
    for (let index = from; index < client.notifications.length; index += 1) {
      const frame = client.notifications[index]
      if (frame.method === 'session.event'
        && frame.params?.sessionId === sessionId
        && frame.params?.event?.type === type) return frame.params.event
    }
    if (Date.now() > deadline) throw new Error(`timeout waiting for ${type} on ${sessionId}`)
    await sleep(20)
  }
}

async function failureOf(promise) {
  try {
    await promise
    return undefined
  } catch (error) {
    return error
  }
}

async function readJsonl(path) {
  const text = await readFile(path, 'utf8')
  return text.split('\n').filter(Boolean).map((line) => JSON.parse(line))
}

const artifacts = join(devRoot, '.dev')
await mkdir(artifacts, { recursive: true })
const mock = await startMockModel()
const mockPatch = join(artifacts, 'trace-mock.patch.yml')
await writeFile(mockPatch, `- id: llm-pi-ai
  config:
    providers:
      bole:
        displayName: Bole Model
        api: openai-completions
        baseURL: ${mock.url}
        apiKeyEnv: BOLE_API_KEY
        models:
          - id: qwen3.8-27b
`)

const home = await prepareHome('bole-trace-')
await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')
let client
try {
  await ensureDevLinks()
  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })
  await client.request('session/create', { sessionId: 's-trace' })
  for (const text of ['first turn', 'second turn']) {
    const mark = client.notifications.length
    await client.request('session/prompt', { sessionId: 's-trace', contentBlocks: [{ type: 'text', text }] })
    await waitEvent(client, 's-trace', 'turn/end', mark)
  }

  const allPath = join(home, 'trace-all.jsonl')
  const all = await client.request('session/trace', { sessionId: 's-trace', path: allPath })
  assert(all.eventCount > 0 && all.path === allPath, `trace all returned ${JSON.stringify(all)}`)
  const allEvents = await readJsonl(allPath)
  assert(allEvents.filter((event) => event.type === 'turn/start').length === 2, 'trace all missing turns')
  assert(allEvents.filter((event) => event.type === 'turn/end').length === 2, 'trace all missing turn ends')

  const secondPath = join(home, 'trace-turn-2.jsonl')
  const second = await client.request('session/trace', { sessionId: 's-trace', turn: 2, path: secondPath })
  const secondEvents = await readJsonl(secondPath)
  assert(second.eventCount === secondEvents.length, 'trace turn count does not match the file')
  assert(secondEvents.filter((event) => event.type === 'turn/start').every((event) => event.data.turn === 2),
    'trace turn 2 contains another turn')
  assert(secondEvents.filter((event) => event.type === 'turn/start').length === 1, 'trace turn 2 missing its turn/start')
  assert(secondEvents.filter((event) => event.type === 'user/message').length === 1, 'trace turn 2 missing the user message')

  const emptyPath = join(home, 'trace-empty.jsonl')
  const empty = await client.request('session/trace', { sessionId: 's-trace', turn: 99, path: emptyPath })
  assert(empty.eventCount === 0, `missing turn produced ${empty.eventCount} events`)

  const missingParent = await failureOf(client.request('session/trace', {
    sessionId: 's-trace',
    path: join(home, 'missing', 'trace.jsonl'),
  }))
  assert(missingParent?.rpc?.data?.code === 'TRACE_WRITE_FAILED', `missing parent was not TRACE_WRITE_FAILED: ${JSON.stringify(missingParent?.rpc)}`)

  const unknown = await failureOf(client.request('session/trace', { sessionId: 's-unknown', path: allPath }))
  assert(unknown?.rpc?.data?.code === 'SESSION_NOT_FOUND', `unknown session was not SESSION_NOT_FOUND: ${JSON.stringify(unknown?.rpc)}`)

  await client.request('shutdown', {}, 15_000)
  const firstExit = await client.waitExit(20_000)
  assert(firstExit.code === 0, `first runtime exit ${String(firstExit.code)}`)

  // Cold export: the second process never resumed the session, so trace must
  // read it through sessionQuery rather than the live registry.
  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })
  const coldPath = join(home, 'trace-cold.jsonl')
  const cold = await client.request('session/trace', { sessionId: 's-trace', path: coldPath })
  const coldEvents = await readJsonl(coldPath)
  assert(cold.eventCount === coldEvents.length && cold.eventCount > 0, `cold trace returned ${JSON.stringify(cold)}`)
  assert(coldEvents.filter((event) => event.type === 'turn/start').length === 2, 'cold trace lost a turn')
  await client.request('shutdown', {}, 15_000)
  const secondExit = await client.waitExit(20_000)
  assert(secondExit.code === 0, `second runtime exit ${String(secondExit.code)}`)

  console.log('PASS trace live: all turns and turn=2 filter')
  console.log('PASS trace errors: TRACE_WRITE_FAILED, SESSION_NOT_FOUND')
  console.log('PASS trace cold export after restart')
} catch (error) {
  console.error(`FAIL trace: ${error instanceof Error ? error.message : String(error)}`)
  if (client !== undefined) console.error(client.stderrTail(40))
  process.exitCode = 1
} finally {
  client?.closeInput()
  await mock.close()
  await cleanup(home)
}
