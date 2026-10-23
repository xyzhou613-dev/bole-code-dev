#!/usr/bin/env node
/**
 * Zero-dependency MCP server for SDK acceptance runs.
 *
 * Speaks newline-delimited JSON-RPC 2.0 over stdio and exposes deterministic
 * side-effect-free tools plus controlled error/delay/large-result probes.
 * Stdout is protocol-only; diagnostics go to stderr.
 *
 * Usage:
 *   node examples/toy-mcp/server.mjs
 */

import { createInterface } from 'node:readline'
import { createHash, randomUUID } from 'node:crypto'

const SERVER_INFO = { name: 'bole-toy-mcp', version: '0.2.0' }
const PROTOCOL_VERSION = '2025-06-18'

const TOOLS = [
  {
    name: 'add',
    description: 'Add two numbers. No side effects.',
    inputSchema: {
      type: 'object',
      properties: {
        a: { type: 'number', description: 'First addend' },
        b: { type: 'number', description: 'Second addend' },
      },
      required: ['a', 'b'],
      additionalProperties: false,
    },
  },
  {
    name: 'subtract',
    description: 'Subtract b from a. No side effects.',
    inputSchema: {
      type: 'object',
      properties: {
        a: { type: 'number', description: 'Minuend' },
        b: { type: 'number', description: 'Subtrahend' },
      },
      required: ['a', 'b'],
      additionalProperties: false,
    },
  },
  {
    name: 'multiply',
    description: 'Multiply two numbers. No side effects.',
    inputSchema: {
      type: 'object',
      properties: {
        a: { type: 'number', description: 'First factor' },
        b: { type: 'number', description: 'Second factor' },
      },
      required: ['a', 'b'],
      additionalProperties: false,
    },
  },
  {
    name: 'echo',
    description: 'Return the provided text. No side effects.',
    inputSchema: {
      type: 'object',
      properties: { text: { type: 'string', description: 'Text to return' } },
      required: ['text'],
      additionalProperties: false,
    },
  },
  {
    name: 'now',
    description: 'Return the current time as ISO-8601. No side effects.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'word_count',
    description: 'Count characters, words and lines in text. No side effects.',
    inputSchema: {
      type: 'object',
      properties: { text: { type: 'string', description: 'Text to count' } },
      required: ['text'],
      additionalProperties: false,
    },
  },
  {
    name: 'sha256',
    description: 'Return the SHA-256 hex digest of UTF-8 text. No side effects.',
    inputSchema: {
      type: 'object',
      properties: { text: { type: 'string', description: 'Text to hash' } },
      required: ['text'],
      additionalProperties: false,
    },
  },
  {
    name: 'json_get',
    description: 'Parse a JSON string and return one value by dot path, for example a.b.0. No side effects.',
    inputSchema: {
      type: 'object',
      properties: {
        json: { type: 'string', description: 'JSON text' },
        path: { type: 'string', description: 'Dot-separated object keys or array indexes' },
      },
      required: ['json', 'path'],
      additionalProperties: false,
    },
  },
  {
    name: 'uuid',
    description: 'Return a random UUID v4. No side effects.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'sleep',
    description: 'Wait for the requested milliseconds (0-5000), then return. Used to test cancellation and timeouts.',
    inputSchema: {
      type: 'object',
      properties: {
        ms: { type: 'number', description: 'Delay in milliseconds, 0-5000' },
      },
      required: ['ms'],
      additionalProperties: false,
    },
  },
  {
    name: 'big_text',
    description: 'Return a repeated ASCII string of the requested byte size (1-65536). Used to test result truncation.',
    inputSchema: {
      type: 'object',
      properties: {
        bytes: { type: 'number', description: 'Number of ASCII bytes to return, 1-65536' },
      },
      required: ['bytes'],
      additionalProperties: false,
    },
  },
  {
    name: 'fail',
    description: 'Always return an MCP tool error with the provided message. Used to test tool-error handling.',
    inputSchema: {
      type: 'object',
      properties: {
        message: { type: 'string', description: 'Error message to return' },
      },
      required: ['message'],
      additionalProperties: false,
    },
  },
]

function textResult(text) {
  return { isError: false, text: String(text) }
}

function errorResult(text) {
  return { isError: true, text: String(text) }
}

function numberArgument(value, name) {
  return typeof value === 'number' && Number.isFinite(value) ? undefined : `${name} must be a finite number`
}

/**
 * Run one tool and return MCP content blocks.
 * @param name - tool name.
 * @param args - parsed arguments object.
 * @returns text content or an error result.
 */
async function callTool(name, args) {
  switch (name) {
    case 'add':
      return textResult(args.a + args.b)
    case 'subtract':
      return textResult(args.a - args.b)
    case 'multiply':
      return textResult(args.a * args.b)
    case 'echo':
      if (typeof args.text !== 'string') return errorResult('echo requires string text')
      return textResult(args.text)
    case 'now':
      return textResult(new Date().toISOString())
    case 'word_count': {
      if (typeof args.text !== 'string') return errorResult('word_count requires string text')
      const words = args.text.trim() === '' ? 0 : args.text.trim().split(/\s+/u).length
      return textResult(JSON.stringify({
        characters: [...args.text].length,
        words,
        lines: args.text === '' ? 0 : args.text.split(/\r\n|\r|\n/u).length,
      }))
    }
    case 'sha256':
      if (typeof args.text !== 'string') return errorResult('sha256 requires string text')
      return textResult(createHash('sha256').update(args.text, 'utf8').digest('hex'))
    case 'json_get': {
      if (typeof args.json !== 'string' || typeof args.path !== 'string') {
        return errorResult('json_get requires string json and path')
      }
      let value
      try {
        value = JSON.parse(args.json)
      } catch {
        return errorResult('json_get could not parse json')
      }
      if (args.path !== '') {
        for (const segment of args.path.split('.')) {
          if (Array.isArray(value)) {
            if (!/^\d+$/u.test(segment) || Number(segment) >= value.length) {
              return errorResult(`json_get path not found: ${args.path}`)
            }
            value = value[Number(segment)]
          } else if (value !== null && typeof value === 'object' && Object.hasOwn(value, segment)) {
            value = value[segment]
          } else {
            return errorResult(`json_get path not found: ${args.path}`)
          }
        }
      }
      return textResult(value === undefined ? 'null' : JSON.stringify(value))
    }
    case 'uuid':
      return textResult(randomUUID())
    case 'sleep': {
      if (!Number.isFinite(args.ms) || args.ms < 0 || args.ms > 5000) {
        return errorResult('sleep ms must be between 0 and 5000')
      }
      await new Promise((resolve) => setTimeout(resolve, args.ms))
      return textResult(`slept ${args.ms} ms`)
    }
    case 'big_text': {
      if (!Number.isInteger(args.bytes) || args.bytes < 1 || args.bytes > 65536) {
        return errorResult('big_text bytes must be an integer between 1 and 65536')
      }
      return textResult('x'.repeat(args.bytes))
    }
    case 'fail':
      return errorResult(typeof args.message === 'string' ? args.message : 'requested failure')
    default:
      return errorResult(`unknown tool: ${String(name)}`)
  }
}

/**
 * Write one protocol frame to stdout.
 * @param message - JSON-RPC response object.
 */
function write(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`)
}

/**
 * Handle one parsed request.
 * @param request - parsed JSON-RPC object.
 */
async function handle(request) {
  if (request === null || typeof request !== 'object') return
  const id = request.id
  const method = request.method
  if (typeof method !== 'string') return
  if (method === 'notifications/initialized' || method === 'notifications/cancelled') return
  if (id === undefined) return
  switch (method) {
    case 'initialize':
      write({
        jsonrpc: '2.0',
        id,
        result: {
          protocolVersion: typeof request.params?.protocolVersion === 'string'
            ? request.params.protocolVersion
            : PROTOCOL_VERSION,
          capabilities: { tools: { listChanged: false } },
          serverInfo: SERVER_INFO,
        },
      })
      return
    case 'ping':
      write({ jsonrpc: '2.0', id, result: {} })
      return
    case 'tools/list':
      write({ jsonrpc: '2.0', id, result: { tools: TOOLS } })
      return
    case 'tools/call': {
      const name = request.params?.name
      const args = request.params?.arguments
      if (typeof name !== 'string' || args === null || typeof args !== 'object' || Array.isArray(args)) {
        write({ jsonrpc: '2.0', id, error: { code: -32602, message: 'invalid tools/call params' } })
        return
      }
      for (const key of ['a', 'b']) {
        if ((name === 'add' || name === 'subtract' || name === 'multiply') && numberArgument(args[key], key) !== undefined) {
          write({ jsonrpc: '2.0', id, result: { content: [{ type: 'text', text: `${name} requires numeric a and b` }], isError: true } })
          return
        }
      }
      const result = await callTool(name, args)
      write({
        jsonrpc: '2.0',
        id,
        result: {
          content: [{ type: 'text', text: result.text }],
          isError: result.isError,
        },
      })
      return
    }
    case 'resources/list':
      write({ jsonrpc: '2.0', id, result: { resources: [] } })
      return
    case 'prompts/list':
      write({ jsonrpc: '2.0', id, result: { prompts: [] } })
      return
    default:
      write({ jsonrpc: '2.0', id, error: { code: -32601, message: `method not found: ${method}` } })
  }
}

createInterface({ input: process.stdin }).on('line', (line) => {
  const trimmed = line.trim()
  if (trimmed === '') return
  try {
    void handle(JSON.parse(trimmed)).catch((error) => {
      process.stderr.write(`[toy-mcp] request failed: ${error instanceof Error ? error.message : String(error)}\n`)
    })
  } catch (error) {
    process.stderr.write(`[toy-mcp] invalid frame: ${error instanceof Error ? error.message : String(error)}\n`)
  }
})
