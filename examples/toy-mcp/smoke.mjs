#!/usr/bin/env node
/** Contract smoke for the toy MCP server: initialize, list, call every tool. */
import { spawn } from 'node:child_process'
import { createInterface } from 'node:readline'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { createHash } from 'node:crypto'

const here = dirname(fileURLToPath(import.meta.url))
const child = spawn(process.execPath, [join(here, 'server.mjs')], { stdio: ['pipe', 'pipe', 'inherit'] })
const lines = createInterface({ input: child.stdout })
let nextId = 1
const pending = new Map()

lines.on('line', (line) => {
  const message = JSON.parse(line)
  const resolve = pending.get(message.id)
  if (resolve === undefined) return
  pending.delete(message.id)
  resolve(message)
})

function request(method, params) {
  const id = nextId++
  const promise = new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`timeout: ${method}`)) }, 5000)
    pending.set(id, (message) => { clearTimeout(timer); resolve(message) })
  })
  child.stdin.write(`${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`)
  return promise
}

function contentText(message) {
  return message.result?.content?.[0]?.text
}

const initialized = await request('initialize', {
  protocolVersion: '2025-06-18',
  capabilities: {},
  clientInfo: { name: 'toy-smoke', version: '1' },
})
if (initialized.result?.serverInfo?.version !== '0.2.0') throw new Error('bad server version')

const listed = await request('tools/list', {})
const names = listed.result.tools.map((tool) => tool.name).sort()
const expected = ['add', 'big_text', 'echo', 'fail', 'json_get', 'multiply', 'now', 'sha256', 'sleep', 'subtract', 'uuid', 'word_count']
if (JSON.stringify(names) !== JSON.stringify(expected)) throw new Error(`tool list mismatch: ${names.join(',')}`)

const calls = [
  ['add', { a: 2, b: 3 }, '5', false],
  ['subtract', { a: 9, b: 4 }, '5', false],
  ['multiply', { a: 3, b: 4 }, '12', false],
  ['echo', { text: 'hello' }, 'hello', false],
  ['word_count', { text: 'one two\nthree' }, '{"characters":13,"words":3,"lines":2}', false],
  ['sha256', { text: 'abc' }, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad', false],
  ['json_get', { json: '{"a":{"b":[7,8]}}', path: 'a.b.0' }, '7', false],
  ['sleep', { ms: 50 }, 'slept 50 ms', false],
  ['big_text', { bytes: 10000 }, null, false],
  ['fail', { message: 'expected failure' }, 'expected failure', true],
]
for (const [name, arguments_, expectedText, expectedError] of calls) {
  const message = await request('tools/call', { name, arguments: arguments_ })
  if (message.result?.isError !== expectedError) throw new Error(`${name}: isError mismatch`)
  const text = contentText(message)
  if (expectedText === null) {
    if (typeof text !== 'string' || text.length !== 10000) throw new Error('big_text length mismatch')
  } else if (text !== expectedText) {
    throw new Error(`${name}: expected ${expectedText}, got ${text}`)
  }
}

const now = await request('tools/call', { name: 'now', arguments: {} })
if (Number.isNaN(Date.parse(contentText(now)))) throw new Error('now is not ISO-8601')
const uuid = await request('tools/call', { name: 'uuid', arguments: {} })
if (!/^[0-9a-f-]{36}$/u.test(contentText(uuid))) throw new Error('uuid malformed')
if (createHash('sha256').update('abc').digest('hex') !== 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad') {
  throw new Error('test vector changed')
}

child.stdin.end()
await new Promise((resolve) => child.once('exit', resolve))
console.log(`PASS toy MCP: ${names.length} tools`)
