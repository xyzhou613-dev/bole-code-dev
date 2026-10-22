/**
 * A7 end-to-end: real runtime + mock model + toy MCP server.
 *
 * One prompt must produce a model tool call, an MCP tool result, and a final
 * assistant message within the acceptance budget (< 1 minute).
 *
 * Usage: node runtime/dev/e2e.mjs
 */

import { mkdir, writeFile } from 'node:fs/promises'
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

async function waitEvent(client, sessionId, type, from = 0, timeoutMs = 30_000) {
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

const artifacts = join(devRoot, '.dev')
await mkdir(artifacts, { recursive: true })
const mock = await startMockModel({
  reply: (body) => {
    const messages = Array.isArray(body.messages) ? body.messages : []
    if (messages.at(-1)?.role === 'tool') return { text: 'tool result received' }
    return { toolCall: { name: 'mcp__toy__add', arguments: '{"a":2,"b":3}' } }
  },
})
const mockPatch = join(artifacts, 'e2e-mock.patch.yml')
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

const mcpPatch = join(artifacts, 'e2e-mcp.patch.yml')
await writeFile(mcpPatch, `- insert:
    - id: mcp-toy
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        transport: stdio
        serverName: toy
        command: ${JSON.stringify(process.execPath)}
        args: [${JSON.stringify(join(repoRoot, 'examples/toy-mcp/server.mjs'))}]
        cwd: ${JSON.stringify(repoRoot)}
        toolCallTimeoutMs: 10000
        failOnStartupError: true
`)

const home = await prepareHome('bole-e2e-')
await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')
let client
try {
  await ensureDevLinks()
  const started = Date.now()
  client = new RpcClient(spawnRuntime({ home, patches: [devPatchFile, mockPatch, mcpPatch] }))
  await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })
  await client.request('session/create', { sessionId: 's-e2e' })
  const mark = client.notifications.length
  await client.request('session/prompt', { sessionId: 's-e2e', contentBlocks: [{ type: 'text', text: '用玩具工具计算 2+3' }] })
  await waitEvent(client, 's-e2e', 'turn/end', mark)
  const terminalDeadline = Date.now() + 5_000
  while (!client.notifications.some((frame) => frame.method === 'bole.event'
    && frame.params?.sessionId === 's-e2e'
    && (frame.params.event.type === 50 || frame.params.event.type === 51))) {
    if (Date.now() > terminalDeadline) throw new Error('projected terminal event was not observed')
    await sleep(10)
  }
  const elapsed = Date.now() - started

  const events = client.notifications
    .slice(mark)
    .filter((frame) => frame.method === 'bole.event' && frame.params?.sessionId === 's-e2e')
    .map((frame) => frame.params.event)
  const call = events.find((event) => event.type === 30)
  const result = events.find((event) => event.type === 31)
  const message = events.filter((event) => event.type === 22).at(-1)
  const terminal = events.find((event) => event.type === 50 || event.type === 51)

  assert(call?.payload.tool === 'mcp__toy__add', `tool call mismatch: ${JSON.stringify(call?.payload)}`)
  assert(call.payload.args?.a === 2 && call.payload.args?.b === 3, `tool args mismatch: ${JSON.stringify(call.payload.args)}`)
  assert(result?.payload.isError === false, `tool result failed: ${JSON.stringify(result?.payload)}`)
  assert(result.payload.resultSummary.trim() === '5', `tool result mismatch: ${JSON.stringify(result.payload)}`)
  assert(message?.payload.text === 'tool result received', `final message mismatch: ${JSON.stringify(message?.payload)}`)
  assert(terminal?.type === 50 && terminal.payload.outcome === 'completed', `turn did not complete: ${JSON.stringify(terminal)}`)
  assert(elapsed < 60_000, `acceptance budget exceeded: ${elapsed}ms`)

  console.log('PASS MCP tool call: mcp__toy__add(2,3) -> 5')
  console.log('PASS final assistant message and completed turn')
  console.log(`PASS E2E elapsed: ${elapsed}ms (budget 60000ms)`)

  await client.request('shutdown', {}, 15_000)
  const exit = await client.waitExit(20_000)
  assert(exit.code === 0, `runtime exit ${String(exit.code)}`)
} catch (error) {
  console.error(`FAIL e2e: ${error instanceof Error ? error.message : String(error)}`)
  if (client !== undefined) console.error(client.stderrTail(60))
  process.exitCode = 1
} finally {
  client?.closeInput()
  await mock.close()
  await cleanup(home)
}
