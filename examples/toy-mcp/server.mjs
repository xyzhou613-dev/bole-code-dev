#!/usr/bin/env node
/**
 * Zero-dependency MCP server for SDK acceptance runs.
 *
 * Speaks newline-delimited JSON-RPC 2.0 over stdio and exposes three
 * side-effect-free tools. Stdout is protocol-only; diagnostics go to stderr.
 *
 * Usage:
 *   node examples/toy-mcp/server.mjs
 */

import { createInterface } from 'node:readline'

const SERVER_INFO = { name: 'bole-toy-mcp', version: '0.1.0' }
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
]

/**
 * Run one tool and return MCP content blocks.
 * @param name - tool name.
 * @param args - parsed arguments object.
 * @returns text content or an error result.
 */
function callTool(name, args) {
  switch (name) {
    case 'add': {
      if (typeof args.a !== 'number' || typeof args.b !== 'number') {
        return { isError: true, text: 'add requires numeric a and b' }
      }
      return { isError: false, text: String(args.a + args.b) }
    }
    case 'echo': {
      if (typeof args.text !== 'string') return { isError: true, text: 'echo requires string text' }
      return { isError: false, text: args.text }
    }
    case 'now':
      return { isError: false, text: new Date().toISOString() }
    default:
      return { isError: true, text: `unknown tool: ${String(name)}` }
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
function handle(request) {
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
      const result = callTool(name, args)
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
    handle(JSON.parse(trimmed))
  } catch (error) {
    process.stderr.write(`[toy-mcp] invalid frame: ${error instanceof Error ? error.message : String(error)}\n`)
  }
})
