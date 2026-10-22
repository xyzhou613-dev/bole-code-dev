/**
 * Stage 0 smoke: boot the real `--profile sdk` runtime with the Bole patch,
 * complete the JSON-RPC handshake, and shut down cleanly.
 *
 * Usage: node runtime/dev/smoke.mjs
 */

import { assert, cleanup, ensureDevLinks, makeHome, repoRoot, RpcClient, spawnRuntime } from './shared/runtime.mjs'

const home = await makeHome('bole-smoke-')
let client
let code = 1
try {
  await ensureDevLinks()
  const started = Date.now()
  const child = spawnRuntime({ home })
  client = new RpcClient(child)

  const init = await client.request('initialize', {
    cwd: repoRoot,
    provider: 'bole',
    model: 'qwen3.8-27b',
  })
  const first = await client.firstFrame
  assert(first.jsonrpc === '2.0', `first stdout frame is not JSON-RPC: ${JSON.stringify(first)}`)
  assert(init?.serverInfo?.name !== undefined, 'initialize result has no serverInfo')
  assert(init.sdkProtocolVersion === 1, `initialize result has no sdkProtocolVersion 1: ${JSON.stringify(init)}`)
  assert(init.capabilities?.preset === true, `preset plane is not composed: ${JSON.stringify(init.capabilities)}`)
  assert(init.capabilities?.persistence === true, `base profile must expose persistence: ${JSON.stringify(init.capabilities)}`)
  assert(init.capabilities?.sandbox === 'danger-full-access', `dev runtime must report its real sandbox mode: ${JSON.stringify(init.capabilities)}`)
  const elapsed = Date.now() - started

  const echo = await client.request('bole/echo', { text: 'hi' })
  assert(echo?.echo?.text === 'hi', `bole/echo did not round-trip params: ${JSON.stringify(echo)}`)

  let unknown
  try {
    await client.request('bogus/method', {})
  } catch (error) {
    unknown = error
  }
  assert(unknown?.rpc?.data?.code === 'METHOD_NOT_FOUND', `unknown method was not mapped to METHOD_NOT_FOUND: ${JSON.stringify(unknown?.rpc)}`)

  client.sendRaw('{not json')
  await new Promise((resolve) => setTimeout(resolve, 200))
  const parseError = client.frames.find((frame) => frame.error?.code === -32700)
  assert(parseError?.error?.data?.code === 'PROTOCOL_PARSE_FAILED', `parse error was not answered with PROTOCOL_PARSE_FAILED: ${JSON.stringify(parseError)}`)

  for (const line of client.lines) {
    let frame
    try {
      frame = JSON.parse(line)
    } catch {
      throw new Error(`stdout carried a non-JSON line: ${line.slice(0, 200)}`)
    }
    assert(frame?.jsonrpc === '2.0', `stdout carried a non-JSON-RPC frame: ${line.slice(0, 200)}`)
  }

  const shutdown = await client.request('shutdown', {}, 15_000)
  assert(shutdown !== null && typeof shutdown === 'object', 'shutdown result is not an object')
  const exit = await client.waitExit(20_000)

  assert(exit.code === 0, `runtime exited with code ${String(exit.code)} signal ${String(exit.signal)}\n${client.stderrTail()}`)
  assert(client.stderr.includes('[bole-sdk-server] loaded'), `plugin did not load:\n${client.stderrTail()}`)
  assert(!client.stderr.includes('did not activate'), `plugin did not activate:\n${client.stderrTail()}`)

  console.log(`PASS initialize: serverInfo=${init.serverInfo.name} version=${init.serverInfo.version ?? 'unknown'} ${elapsed}ms`)
  console.log('PASS protocol: bole/echo, METHOD_NOT_FOUND, PROTOCOL_PARSE_FAILED')
  console.log(`PASS stdout: ${client.lines.length} frame(s), first=${JSON.stringify(first).slice(0, 120)}`)
  console.log(`PASS shutdown: exit=${exit.code}`)
  code = 0
} catch (error) {
  console.error(`FAIL smoke: ${error instanceof Error ? error.message : String(error)}`)
  if (client !== undefined) console.error(client.stderrTail(40))
} finally {
  client?.closeInput()
  await cleanup(home)
  process.exitCode = code
}
