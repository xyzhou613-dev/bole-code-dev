/**
 * Transport conformance tests: framing, frame bound, and stable error names.
 */

import { PassThrough } from 'node:stream'
import { describe, expect, it } from 'vitest'
import { BoleLineTransport } from '../src/transport.ts'

interface Harness {
  input: PassThrough
  output: PassThrough
  frames: Record<string, any>[]
  fatal: unknown[]
  close: () => void
}

/**
 * Start a transport over in-memory streams and collect parsed output frames.
 * @param maxLineBytes - frame bound for the test.
 */
async function harness(maxLineBytes = 4096): Promise<Harness> {
  const input = new PassThrough()
  const output = new PassThrough()
  const frames: Record<string, any>[] = []
  const fatal: unknown[] = []
  let buffered = ''
  output.setEncoding('utf8')
  output.on('data', (chunk: string) => {
    buffered += chunk
    for (;;) {
      const newline = buffered.indexOf('\n')
      if (newline < 0) break
      const line = buffered.slice(0, newline)
      buffered = buffered.slice(newline + 1)
      if (line !== '') frames.push(JSON.parse(line))
    }
  })
  const transport = new BoleLineTransport(input, output, {
    maxLineBytes,
    onFatal: (error) => { fatal.push(error) },
  })
  transport.onRequest(async (method, params) => ({ method, params }))
  transport.start()
  return {
    input,
    output,
    frames,
    fatal,
    close: () => { transport.close(); input.destroy(); output.destroy() },
  }
}

/** Wait until a predicate holds or fail the test. */
async function eventually(predicate: () => boolean, timeoutMs = 1000): Promise<void> {
  const deadline = Date.now() + timeoutMs
  while (!predicate()) {
    if (Date.now() > deadline) throw new Error('condition not met before timeout')
    await new Promise((resolve) => setTimeout(resolve, 5))
  }
}

describe('BoleLineTransport', () => {
  it('round-trips a valid request', async () => {
    const test = await harness()
    try {
      test.input.write(`${JSON.stringify({ jsonrpc: '2.0', id: 7, method: 'echo', params: { a: 1 } })}\n`)
      await eventually(() => test.frames.length === 1)
      expect(test.frames[0]).toEqual({ jsonrpc: '2.0', id: 7, result: { method: 'echo', params: { a: 1 } } })
    } finally {
      test.close()
    }
  })

  it('answers a frame over the bound with LINE_TOO_LONG and closes', async () => {
    const test = await harness(16)
    try {
      test.input.write(`${'x'.repeat(32)}\n`)
      await eventually(() => test.frames.length === 1)
      expect(test.frames[0]).toMatchObject({
        jsonrpc: '2.0',
        id: null,
        error: { code: -32600, data: { code: 'LINE_TOO_LONG' } },
      })
      expect(test.fatal).toHaveLength(1)
    } finally {
      test.close()
    }
  })

  it('rejects batch requests with INVALID_ARGUMENT', async () => {
    const test = await harness()
    try {
      test.input.write(`${JSON.stringify([{ jsonrpc: '2.0', id: 1, method: 'echo' }])}\n`)
      await eventually(() => test.frames.length === 1)
      expect(test.frames[0]).toMatchObject({
        id: null,
        error: { code: -32600, data: { code: 'INVALID_ARGUMENT' } },
      })
    } finally {
      test.close()
    }
  })

  it('rejects array params with INVALID_ARGUMENT', async () => {
    const test = await harness()
    try {
      test.input.write(`${JSON.stringify({ jsonrpc: '2.0', id: 2, method: 'echo', params: [] })}\n`)
      await eventually(() => test.frames.length === 1)
      expect(test.frames[0]).toMatchObject({
        id: 2,
        error: { code: -32602, data: { code: 'INVALID_ARGUMENT' } },
      })
    } finally {
      test.close()
    }
  })

  it('answers malformed JSON with PROTOCOL_PARSE_FAILED', async () => {
    const test = await harness()
    try {
      test.input.write('{not json\n')
      await eventually(() => test.frames.length === 1)
      expect(test.frames[0]).toMatchObject({
        id: null,
        error: { code: -32700, data: { code: 'PROTOCOL_PARSE_FAILED' } },
      })
    } finally {
      test.close()
    }
  })
})
