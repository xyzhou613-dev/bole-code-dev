/**
 * Capture raw wire frames from one real runtime run.
 *
 * Stage 0 skeleton: boots the runtime, sends one prompt through the mock model,
 * waits for the turn to end, and writes every stdout line to
 * `runtime/.dev/<scenario>.jsonl`. Fixture sanitization and manifest generation
 * are added with the event projection (A3).
 *
 * Usage: node runtime/dev/capture.mjs --scenario happy-tool [--text "..."]
 */

import { mkdir, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { assert, cleanup, devPatchFile, devRoot, ensureDevLinks, prepareHome, repoRoot, RpcClient, spawnRuntime } from './shared/runtime.mjs'
import { startMockModel } from './mock-model.mjs'

const args = process.argv.slice(2)
const scenarioFlag = args.indexOf('--scenario')
const scenario = scenarioFlag === -1 ? 'capture' : args[scenarioFlag + 1] ?? 'capture'
const cancel = args.includes('--cancel')
const valueOf = (name, fallback) => {
  const index = args.indexOf(name)
  return index === -1 ? fallback : args[index + 1]
}
const preset = valueOf('--preset')
const model = valueOf('--model', 'qwen3.8-27b')
const textFlag = args.indexOf('--text')
const prompt = textFlag === -1 ? '用一句话介绍你自己' : args[textFlag + 1] ?? 'hello'

const devArtifacts = join(devRoot, '.dev')
await mkdir(devArtifacts, { recursive: true })
const mock = await startMockModel({ delayMs: cancel ? 1200 : 0 })
const mockPatch = join(devArtifacts, 'mock.patch.yml')
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

const home = await prepareHome('bole-capture-')
await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')
let client
try {
  await ensureDevLinks()
  const child = spawnRuntime({ home, patches: [devPatchFile, mockPatch] })
  client = new RpcClient(child)
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model })
  if (preset !== undefined) {
    await client.request('session/create', { sessionId: 's-001', preset })
  }
  await client.request('session/prompt', {
    sessionId: 's-001',
    contentBlocks: [{ type: 'text', text: prompt }],
  })
  if (cancel) {
    const started = Date.now()
    while (Date.now() - started < 10_000
      && !client.notifications.some((frame) => frame.method === 'bole.event'
        && frame.params?.sessionId === 's-001'
        && frame.params?.event?.type === 14)) {
      await new Promise((resolve) => setTimeout(resolve, 20))
    }
    await new Promise((resolve) => setTimeout(resolve, 300))
    await client.request('session/cancel', { sessionId: 's-001' })
  }

  const deadline = Date.now() + 30_000
  while (Date.now() < deadline) {
    if (client.notifications.some((frame) => frame.method === 'session.event' && frame.params?.event?.type === 'turn/end')) break
    await new Promise((resolve) => setTimeout(resolve, 50))
  }
  const ended = client.notifications.some((frame) => frame.method === 'session.event' && frame.params?.event?.type === 'turn/end')
  assert(ended, 'turn/end was not observed before timeout')

  const output = join(devArtifacts, `${scenario}.jsonl`)
  await writeFile(output, `${client.lines.join('\n')}\n`)
  console.log(`captured ${client.lines.length} frame(s) -> ${output}`)
  console.log(`mock calls: ${mock.calls.length}`)
} catch (error) {
  console.error(`FAIL capture: ${error instanceof Error ? error.message : String(error)}`)
  if (client !== undefined) console.error(client.stderrTail(40))
  process.exitCode = 1
} finally {
  client?.closeInput()
  await mock.close()
  await cleanup(home)
}
