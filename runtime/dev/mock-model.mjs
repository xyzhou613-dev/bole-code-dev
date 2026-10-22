/**
 * Deterministic OpenAI chat-completions mock for local SDK runs.
 *
 * The runtime's `bole` provider uses `openai-completions`; this server answers
 * streamed chat completions with either plain text or one tool call, so tests
 * never depend on the on-site model gateway.
 *
 * Usage:
 *   node runtime/dev/mock-model.mjs [--port 0]
 *   MOCK_TOOL=add MOCK_TEXT='calling add' node runtime/dev/mock-model.mjs
 */

import { createServer } from 'node:http'
import { pathToFileURL } from 'node:url'

/**
 * Split one reply into small SSE deltas.
 * @param text - full reply text.
 * @returns chunks of one to four characters.
 */
function chunksOf(text) {
  const chunks = []
  for (let index = 0; index < text.length; index += 4) chunks.push(text.slice(index, index + 4))
  return chunks
}

/**
 * Build the model reply for one request body.
 * @param body - chat-completions request body.
 * @returns text reply or one tool call.
 */
function defaultReply(body) {
  const messages = Array.isArray(body.messages) ? body.messages : []
  const last = messages.at(-1)
  if (last?.role === 'tool') {
    return { text: process.env.MOCK_TOOL_RESULT_TEXT ?? 'tool result received' }
  }
  const tool = process.env.MOCK_TOOL
  if (tool !== undefined && tool !== '') {
    return {
      toolCall: {
        name: tool,
        arguments: process.env.MOCK_TOOL_ARGS ?? '{"a":1,"b":2}',
      },
    }
  }
  return { text: process.env.MOCK_TEXT ?? 'mock reply from bole dev model' }
}

/**
 * Start the mock server.
 * @param options - port hint and optional reply function.
 * @returns listening URL, recorded calls, and a close function.
 */
export async function startMockModel({ port = 0, reply = defaultReply, delayMs = Number(process.env.MOCK_DELAY_MS ?? 0), httpError = Number(process.env.MOCK_HTTP_ERROR ?? 0), finishReason = process.env.MOCK_FINISH ?? 'stop' } = {}) {
  const calls = []
  const server = createServer((request, response) => {
    if (request.method !== 'POST') {
      response.writeHead(404).end()
      return
    }
    const chunks = []
    request.on('data', (chunk) => { chunks.push(chunk) })
    request.on('end', async () => {
      let body = {}
      try {
        body = JSON.parse(Buffer.concat(chunks).toString('utf8'))
      } catch {
        response.writeHead(400).end()
        return
      }
      calls.push(body)
      if (delayMs > 0) await new Promise((resolve) => setTimeout(resolve, delayMs))
      if (httpError > 0) {
        response.writeHead(httpError, { 'content-type': 'application/json' })
        response.end(JSON.stringify({ error: { message: 'mock upstream error', type: 'server_error' } }))
        return
      }
      const result = reply(body)
      const id = `chatcmpl-mock-${calls.length}`
      const model = body.model ?? 'qwen3.8-27b'
      if (body.stream === false) {
        const message = result.toolCall === undefined
          ? { role: 'assistant', content: result.text }
          : {
              role: 'assistant',
              content: null,
              tool_calls: [{
                id: 'call_mock_1',
                type: 'function',
                function: result.toolCall,
              }],
            }
        response.writeHead(200, { 'content-type': 'application/json' })
        response.end(JSON.stringify({
          id,
          object: 'chat.completion',
          created: Math.floor(Date.now() / 1000),
          model,
          choices: [{
            index: 0,
            message,
            finish_reason: result.toolCall === undefined ? 'stop' : 'tool_calls',
          }],
        }))
        return
      }
      response.writeHead(200, {
        'content-type': 'text/event-stream',
        'cache-control': 'no-cache',
        connection: 'keep-alive',
      })
      const write = (delta, finishReason = null) => {
        response.write(`data: ${JSON.stringify({
          id,
          object: 'chat.completion.chunk',
          created: Math.floor(Date.now() / 1000),
          model,
          choices: [{ index: 0, delta, finish_reason: finishReason }],
        })}\n\n`)
      }
      write({ role: 'assistant' })
      if (result.toolCall === undefined) {
        for (const text of chunksOf(result.text)) write({ content: text })
        write({}, finishReason)
      } else {
        write({
          tool_calls: [{
            index: 0,
            id: 'call_mock_1',
            type: 'function',
            function: { name: result.toolCall.name, arguments: result.toolCall.arguments },
          }],
        })
        write({}, 'tool_calls')
      }
      response.write('data: [DONE]\n\n')
      response.end()
    })
  })
  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.listen(port, '127.0.0.1', resolve)
  })
  const address = server.address()
  const actualPort = typeof address === 'object' && address !== null ? address.port : port
  return {
    url: `http://127.0.0.1:${actualPort}/v1`,
    port: actualPort,
    calls,
    close: () => new Promise((resolve) => { server.close(() => resolve()) }),
  }
}

if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const portFlag = process.argv.indexOf('--port')
  const port = portFlag === -1 ? 0 : Number(process.argv[portFlag + 1])
  const mock = await startMockModel({ port })
  console.log(`MOCK_MODEL_URL=${mock.url}`)
  const stop = async () => { await mock.close(); process.exit(0) }
  process.once('SIGINT', () => { void stop() })
  process.once('SIGTERM', () => { void stop() })
}
