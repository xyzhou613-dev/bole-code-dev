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
  prepareHome,
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
          - id: test-model
`)

const home = await prepareHome('bole-session-')
await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')
let client
try {
  await ensureDevLinks()
  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })
  await new Promise((resolve) => setTimeout(resolve, 100))
  const initialRsi = client.notifications.find((frame) => frame.method === 'bole.event'
    && frame.params?.event?.type === 15
    && frame.params.event.payload?.scope === 'default')
  assert(initialRsi?.params?.event?.payload?.preset === 'standard' && initialRsi.params.event.payload.composed === true,
    `initial default RSI state missing: ${JSON.stringify(initialRsi?.params?.event?.payload)}`)

  const created = await client.request('session/create', { sessionId: 's-a2', cwd: repoRoot })
  assert(created?.sessionId === 's-a2', `session/create returned ${JSON.stringify(created)}`)
  assert(created.preset === 'standard', `default preset must be standard, got ${JSON.stringify(created.preset)}`)
  assert(created.cwd === repoRoot, `session/create cwd mismatch: ${JSON.stringify(created.cwd)}`)

  const duplicate = await failureOf(client.request('session/create', { sessionId: 's-a2' }))
  assert(duplicate?.rpc?.data?.code === 'SESSION_EXISTS', `duplicate create was not SESSION_EXISTS: ${JSON.stringify(duplicate?.rpc)}`)

  const rsiSession = await client.request('session/create', { sessionId: 's-a2-rsi', preset: 'rsi-infer' })
  assert(rsiSession.preset === 'rsi-infer', `explicit rsi-infer create failed: ${JSON.stringify(rsiSession)}`)
  const blank = await client.request('session/create', { sessionId: 's-a2-blank' })
  assert(blank.preset === 'standard', `blank session default was not standard: ${JSON.stringify(blank)}`)
  const switched = await client.request('session/setPreset', { sessionId: 's-a2-blank', preset: 'rsi-infer' })
  assert(switched.preset === 'rsi-infer', `blank setPreset failed: ${JSON.stringify(switched)}`)

  const missingResume = await failureOf(client.request('session/resume', { sessionId: 's-missing' }))
  assert(missingResume?.rpc?.data?.code === 'SESSION_NOT_FOUND', `missing resume was not SESSION_NOT_FOUND: ${JSON.stringify(missingResume?.rpc)}`)

  await client.request('session/prompt', { sessionId: 's-a2', contentBlocks: [{ type: 'text', text: 'hello' }] })
  const firstEnd = await waitEvent(client, 's-a2', 'turn/end')
  assert(firstEnd.data.reason.kind === 'completed', `first turn did not complete: ${JSON.stringify(firstEnd.data.reason)}`)
  const projectedDeadline = Date.now() + 5_000
  while (!client.notifications.some((frame) => frame.method === 'bole.event'
    && frame.params?.sessionId === 's-a2'
    && frame.params.event.type === 50)) {
    if (Date.now() > projectedDeadline) throw new Error('projected TASK_FINISHED was not observed')
    await new Promise((resolve) => setTimeout(resolve, 10))
  }
  const projected = client.notifications
    .filter((frame) => frame.method === 'bole.event' && frame.params?.sessionId === 's-a2')
    .map((frame) => frame.params.event.type)
  for (const type of [10, 15, 22, 50]) {
    assert(projected.includes(type), `projected event stream is missing type ${type}: ${JSON.stringify(projected)}`)
  }
  assert(client.notifications.some((frame) => frame.method === 'assistant.delta' && frame.params?.sessionId === 's-a2'), 'no assistant.delta was emitted')

  const idleCancel = await failureOf(client.request('session/cancel', { sessionId: 's-a2' }))
  assert(idleCancel?.rpc?.data?.code === 'NO_ACTIVE_TASK', `idle cancel was not NO_ACTIVE_TASK: ${JSON.stringify(idleCancel?.rpc)}`)

  const mark = client.notifications.length
  await client.request('session/prompt', { sessionId: 's-a2', contentBlocks: [{ type: 'text', text: 'cancel me' }] })
  await waitEvent(client, 's-a2', 'turn/start', mark)
  const canceled = await client.request('session/cancel', { sessionId: 's-a2' })
  assert(canceled !== null && typeof canceled === 'object', `cancel result is not an object: ${JSON.stringify(canceled)}`)
  const aborted = await waitEvent(client, 's-a2', 'turn/end', mark)
  assert(aborted.data.reason.kind === 'aborted', `canceled turn did not end aborted: ${JSON.stringify(aborted.data.reason)}`)

  const locked = await failureOf(client.request('session/setPreset', { sessionId: 's-a2', preset: 'rsi-infer' }))
  assert(locked?.rpc?.data?.code === 'PRESET_LOCKED', `setPreset after the first turn was not PRESET_LOCKED: ${JSON.stringify(locked?.rpc)}`)
  const rsiDefault = await client.request('rsi.get', {})
  assert(rsiDefault.composed === true && rsiDefault.preset === 'standard', `rsi.get default mismatch: ${JSON.stringify(rsiDefault)}`)
  const rsiSet = await client.request('rsi.set', { enabled: true })
  assert(rsiSet.scope === 'default' && rsiSet.preset === 'rsi-infer', `rsi.set default mismatch: ${JSON.stringify(rsiSet)}`)
  const rsiAfter = await client.request('rsi.get', {})
  assert(rsiAfter.enabled === true, `rsi.get did not observe the new default: ${JSON.stringify(rsiAfter)}`)

  const deliveredMark = client.notifications.length
  await client.request('session/prompt', { sessionId: 's-a2-rsi', contentBlocks: [{ type: 'text', text: '启动一个服务并保持运行' }] })
  await waitEvent(client, 's-a2-rsi', 'turn/end', deliveredMark)
  const delivered = client.notifications
    .slice(deliveredMark)
    .filter((frame) => frame.method === 'bole.event' && frame.params?.sessionId === 's-a2-rsi'
      && frame.params.event.type === 15 && frame.params.event.payload?.source === 'receipt')
    .at(-1)
  assert(delivered?.params?.event?.payload?.delivery?.status === 'delivered',
    `rsi-infer receipt was not delivered: ${JSON.stringify(delivered?.params?.event?.payload)}`)
  assert(delivered.params.event.payload.delivery.deliveredMemoryIds.length > 0, 'delivered receipt has no memory ids')

  const availableSession = await client.request('session/create', { sessionId: 's-a2-available', preset: 'rsi-infer' })
  assert(availableSession.preset === 'rsi-infer', 'could not create the rsi-infer available session')
  const availableMark = client.notifications.length
  await client.request('session/prompt', { sessionId: 's-a2-available', contentBlocks: [{ type: 'text', text: 'hello' }] })
  await waitEvent(client, 's-a2-available', 'turn/end', availableMark)
  const available = client.notifications
    .slice(availableMark)
    .filter((frame) => frame.method === 'bole.event' && frame.params?.sessionId === 's-a2-available'
      && frame.params.event.type === 15 && frame.params.event.payload?.source === 'receipt')
    .at(-1)
  assert(available?.params?.event?.payload?.delivery?.status === 'available',
    `rsi-infer receipt was not available: ${JSON.stringify(available?.params?.event?.payload)}`)

  const standardReceipt = client.notifications.some((frame) => frame.method === 'bole.event'
    && frame.params?.sessionId === 's-a2'
    && frame.params.event.type === 15
    && frame.params.event.payload?.source === 'receipt')
  assert(!standardReceipt, 'standard session unexpectedly produced an experience receipt')

  await client.request('shutdown', {}, 15_000)
  const firstExit = await client.waitExit(20_000)
  assert(firstExit.code === 0, `first runtime exit ${String(firstExit.code)}\n${client.stderrTail()}`)

  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })
  const resumed = await client.request('session/resume', { sessionId: 's-a2' })
  assert(resumed?.sessionId === 's-a2', `session/resume returned ${JSON.stringify(resumed)}`)
  assert(resumed.cwd === repoRoot, `session/resume cwd mismatch: ${JSON.stringify(resumed.cwd)}`)
  assert(resumed.preset === 'standard', `session/resume preset mismatch: ${JSON.stringify(resumed.preset)}`)

  await client.request('shutdown', {}, 15_000)
  const secondExit = await client.waitExit(20_000)
  assert(secondExit.code === 0, `second runtime exit ${String(secondExit.code)}\n${client.stderrTail()}`)

  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'test-model' })
  await client.request('session/create', { sessionId: 's-a4-unsupported', preset: 'rsi-infer' })
  const unsupportedMark = client.notifications.length
  await client.request('session/prompt', { sessionId: 's-a4-unsupported', contentBlocks: [{ type: 'text', text: 'hello' }] })
  await waitEvent(client, 's-a4-unsupported', 'turn/end', unsupportedMark)
  const unsupported = client.notifications
    .slice(unsupportedMark)
    .filter((frame) => frame.method === 'bole.event' && frame.params?.sessionId === 's-a4-unsupported'
      && frame.params.event.type === 15 && frame.params.event.payload?.source === 'receipt')
    .at(-1)
  assert(unsupported?.params?.event?.payload?.delivery?.status === 'unsupported-model',
    `rsi-infer receipt was not unsupported-model: ${JSON.stringify(unsupported?.params?.event?.payload)}`)
  await client.request('shutdown', {}, 15_000)
  const thirdExit = await client.waitExit(20_000)
  assert(thirdExit.code === 0, `third runtime exit ${String(thirdExit.code)}\n${client.stderrTail()}`)

  console.log('PASS session/create + duplicate SESSION_EXISTS + preset/missing errors')
  console.log('PASS prompt + completed turn + projected events')

  console.log('PASS cancel: idle NO_ACTIVE_TASK, active turn aborted')
  console.log('PASS RSI receipts: delivered / available / unsupported-model; standard has none')
  console.log('PASS setPreset locked + rsi.set/rsi.get')
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
