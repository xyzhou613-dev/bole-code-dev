/**
 * A2 session smoke: create, duplicate, prompt, cancel, setPreset, resume.
 *
 * Uses the real sdk profile, the mock OpenAI-compatible model, and the same
 * DSH_HOME across two runtime processes so resume reads durable state.
 *
 * Usage: node runtime/dev/session.mjs
 */

import { mkdir, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import {
  assert,
  cleanup,
  devPatchFile,
  devRoot,
  ensureDevLinks,
  makeHome,
  repoRoot,
  RpcClient,
  spawnRuntime,
} from './shared/runtime.mjs'
import { startMockModel } from './mock-model.mjs'

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

/**
 * Wait for one session event from a start cursor.
 * @param client - live JSON-RPC client.
 * @param sessionId - session to observe.
 * @param type - session event type.
 * @param from - notification index to start from.
 * @param timeoutMs - failure timeout.
 * @returns the matched event.
 */
async function waitEvent(client, sessionId, type, from = 0, timeoutMs = 20_000) {
  const deadline = Date.now() + timeoutMs
  for (;;) {
    for (let index = from; index < client.notifications.length; index += 1) {
      const frame = client.notifications[index]
      if (frame.method === 'session.event'
        && frame.params?.sessionId === sessionId
        && frame.params?.event?.type === type) {
        return frame.params.event
      }
    }
    if (Date.now() > deadline) {
      throw new Error(`timeout waiting for ${type} on ${sessionId}\n${client.stderrTail()}`)
    }
    await sleep(20)
  }
}

/** Capture a JSON-RPC failure for assertions. */
async function failureOf(promise) {
  try {
    await promise
    return undefined
  } catch (error) {
    return error
  }
}

const artifacts = join(devRoot, '.dev')
await mkdir(artifacts, { recursive: true })
const mock = await startMockModel({ delayMs: 800 })
const mockPatch = join(artifacts, 'session-mock.patch.yml')
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

const home = await makeHome('bole-session-')
await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')
let client
try {
  await ensureDevLinks()
  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })

  const created = await client.request('session/create', { sessionId: 's-a2', cwd: repoRoot })
  assert(created?.sessionId === 's-a2', `session/create returned ${JSON.stringify(created)}`)
  assert(created.preset === '', `base profile must report an empty preset, got ${JSON.stringify(created.preset)}`)
  assert(created.cwd === repoRoot, `session/create cwd mismatch: ${JSON.stringify(created.cwd)}`)

  const duplicate = await failureOf(client.request('session/create', { sessionId: 's-a2' }))
  assert(duplicate?.rpc?.data?.code === 'SESSION_EXISTS', `duplicate create was not SESSION_EXISTS: ${JSON.stringify(duplicate?.rpc)}`)

  const presetOnBase = await failureOf(client.request('session/create', { sessionId: 's-a2-preset', preset: 'standard' }))
  assert(presetOnBase?.rpc?.data?.code === 'PRESET_NOT_FOUND', `preset create without a plane was not PRESET_NOT_FOUND: ${JSON.stringify(presetOnBase?.rpc)}`)

  const missingResume = await failureOf(client.request('session/resume', { sessionId: 's-missing' }))
  assert(missingResume?.rpc?.data?.code === 'SESSION_NOT_FOUND', `missing resume was not SESSION_NOT_FOUND: ${JSON.stringify(missingResume?.rpc)}`)

  await client.request('session/prompt', { sessionId: 's-a2', contentBlocks: [{ type: 'text', text: 'hello' }] })
  const firstEnd = await waitEvent(client, 's-a2', 'turn/end')
  assert(firstEnd.data.reason.kind === 'completed', `first turn did not complete: ${JSON.stringify(firstEnd.data.reason)}`)

  const idleCancel = await failureOf(client.request('session/cancel', { sessionId: 's-a2' }))
  assert(idleCancel?.rpc?.data?.code === 'NO_ACTIVE_TASK', `idle cancel was not NO_ACTIVE_TASK: ${JSON.stringify(idleCancel?.rpc)}`)

  const mark = client.notifications.length
  await client.request('session/prompt', { sessionId: 's-a2', contentBlocks: [{ type: 'text', text: 'cancel me' }] })
  await waitEvent(client, 's-a2', 'turn/start', mark)
  const canceled = await client.request('session/cancel', { sessionId: 's-a2' })
  assert(canceled !== null && typeof canceled === 'object', `cancel result is not an object: ${JSON.stringify(canceled)}`)
  const aborted = await waitEvent(client, 's-a2', 'turn/end', mark)
  assert(aborted.data.reason.kind === 'aborted', `canceled turn did not end aborted: ${JSON.stringify(aborted.data.reason)}`)

  const setPreset = await failureOf(client.request('session/setPreset', { sessionId: 's-a2', preset: 'standard' }))
  assert(setPreset?.rpc?.data?.code === 'RSI_UNAVAILABLE', `setPreset without a preset plane was not RSI_UNAVAILABLE: ${JSON.stringify(setPreset?.rpc)}`)

  await client.request('shutdown', {}, 15_000)
  const firstExit = await client.waitExit(20_000)
  assert(firstExit.code === 0, `first runtime exit ${String(firstExit.code)}\n${client.stderrTail()}`)

  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })
  const resumed = await client.request('session/resume', { sessionId: 's-a2' })
  assert(resumed?.sessionId === 's-a2', `session/resume returned ${JSON.stringify(resumed)}`)
  assert(resumed.cwd === repoRoot, `session/resume cwd mismatch: ${JSON.stringify(resumed.cwd)}`)

  await client.request('shutdown', {}, 15_000)
  const secondExit = await client.waitExit(20_000)
  assert(secondExit.code === 0, `second runtime exit ${String(secondExit.code)}\n${client.stderrTail()}`)

  console.log('PASS session/create + duplicate SESSION_EXISTS + preset/missing errors')
  console.log('PASS prompt + completed turn')
  console.log('PASS cancel: idle NO_ACTIVE_TASK, active turn aborted')
  console.log('PASS setPreset without plane: RSI_UNAVAILABLE')
  console.log('PASS resume after restart')
} catch (error) {
  console.error(`FAIL session: ${error instanceof Error ? error.message : String(error)}`)
  if (client !== undefined) console.error(client.stderrTail(40))
  process.exitCode = 1
} finally {
  client?.closeInput()
  await mock.close()
  await cleanup(home)
}
