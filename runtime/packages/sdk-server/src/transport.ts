/**
 * Strict newline-delimited JSON-RPC 2.0 transport for the Bole SDK runtime.
 *
 * The official transport is permissive by design: it ignores malformed lines,
 * has no frame-size bound, normalizes batch/scalar params to `{}`, and writes
 * errors without the stable `data.code` the SDK contract requires. This
 * transport implements the frozen protocol (02-protocol.md §1 and §4) while
 * keeping the same `JsonRpcTransportPeer` surface the official server expects.
 */

import { StringDecoder } from 'node:string_decoder'
import type { Readable, Writable } from 'node:stream'
import type { JsonRpcTransportPeer } from '@deepseek-ai/dsh-sdk-protocol'

type JsonRpcId = string | number
type RequestHandler = (method: string, params: Record<string, unknown>) => Promise<unknown>
type NotificationHandler = (method: string, params: Record<string, unknown>) => void

/** A protocol error carrying the JSON-RPC code and the stable SDK error name. */
export class BoleRpcError extends Error {
  /**
   * @param code - JSON-RPC error code.
   * @param message - short English diagnostic, log-only.
   * @param data - stable `data` payload; `data.code` is the contract name.
   */
  constructor(
    readonly code: number,
    message: string,
    readonly data: Record<string, unknown> | undefined,
  ) {
    super(message)
    this.name = 'BoleRpcError'
  }
}

/** Transport options. */
export interface BoleTransportOptions {
  /** Maximum UTF-8 bytes per frame, both directions. Default: 64 MiB. */
  maxLineBytes?: number
  /** Called after the transport closes itself on a fatal protocol error. */
  onFatal?: (error: BoleRpcError) => void
}

const DEFAULT_MAX_LINE_BYTES = 64 * 1024 * 1024
const PROTOCOL_PARSE_FAILED = 'PROTOCOL_PARSE_FAILED'
const INVALID_ARGUMENT = 'INVALID_ARGUMENT'
const METHOD_NOT_FOUND = 'METHOD_NOT_FOUND'
const LINE_TOO_LONG = 'LINE_TOO_LONG'
const INTERNAL = 'INTERNAL'

interface PendingRequest {
  resolve: (value: unknown) => void
  reject: (error: Error) => void
}

/**
 * Line-delimited JSON-RPC endpoint over caller-owned streams.
 *
 * Frames are UTF-8, LF-terminated, and bounded by `maxLineBytes`. Malformed
 * JSON is answered with `-32700`; a batch or non-object request with `-32600`;
 * non-object params with `-32602`; an oversized frame closes the endpoint after
 * answering `-32600` + `LINE_TOO_LONG`.
 */
export class BoleLineTransport implements JsonRpcTransportPeer {
  private buffer = ''
  private bufferedBytes = 0
  private readonly decoder = new StringDecoder('utf8')
  private readonly maxLineBytes: number
  private readonly onFatal: ((error: BoleRpcError) => void) | undefined
  private started = false
  private closed = false
  private requestHandler: RequestHandler | undefined
  private notificationHandler: NotificationHandler | undefined
  private readonly pending = new Map<JsonRpcId, PendingRequest>()
  private nextRequestId = 1

  /**
   * @param input - protocol input stream (stdin in production).
   * @param output - protocol output stream (stdout in production).
   * @param options - frame bound and fatal-error hook.
   */
  constructor(
    private readonly input: Readable,
    private readonly output: Writable,
    options: BoleTransportOptions = {},
  ) {
    this.maxLineBytes = options.maxLineBytes ?? DEFAULT_MAX_LINE_BYTES
    this.onFatal = options.onFatal
  }

  /** Attach input listeners; idempotent. */
  start(): void {
    if (this.started || this.closed) return
    this.started = true
    this.input.on('data', this.onData)
    this.input.on('error', this.onInputError)
    this.input.on('end', this.onInputEnd)
  }

  /** Detach listeners and reject pending outbound requests. */
  close(): void {
    if (this.closed) return
    this.closed = true
    this.input.off('data', this.onData)
    this.input.off('error', this.onInputError)
    this.input.off('end', this.onInputEnd)
    const pending = [...this.pending.values()]
    this.pending.clear()
    for (const waiter of pending) waiter.reject(new Error('JSON-RPC transport closed'))
  }

  /**
   * Install the request handler.
   * @param handler - resolves the JSON-RPC result.
   */
  onRequest(handler: RequestHandler): void {
    this.requestHandler = handler
  }

  /**
   * Install the notification handler.
   * @param handler - receives notifications without a response.
   */
  onNotification(handler: NotificationHandler): void {
    this.notificationHandler = handler
  }

  /**
   * Send one outbound request. The server side does not use this today; it
   * exists to satisfy the shared transport peer surface.
   * @param method - JSON-RPC method.
   * @param params - request params.
   * @param signal - optional abandonment signal.
   * @returns the peer result.
   */
  request(method: string, params: object, signal?: AbortSignal): Promise<unknown> {
    const id = `req_${this.nextRequestId++}`
    return new Promise((resolve, reject) => {
      if (signal?.aborted === true) {
        reject(new Error('JSON-RPC request aborted'))
        return
      }
      const onAbort = (): void => {
        this.pending.delete(id)
        reject(new Error('JSON-RPC request aborted'))
      }
      signal?.addEventListener('abort', onAbort, { once: true })
      this.pending.set(id, {
        resolve: (value) => {
          signal?.removeEventListener('abort', onAbort)
          resolve(value)
        },
        reject: (error) => {
          signal?.removeEventListener('abort', onAbort)
          reject(error)
        },
      })
      try {
        this.write({ jsonrpc: '2.0', id, method, params })
      } catch (error) {
        this.pending.delete(id)
        signal?.removeEventListener('abort', onAbort)
        reject(error instanceof Error ? error : new Error(String(error)))
      }
    })
  }

  /**
   * Send one notification.
   * @param method - notification name.
   * @param params - optional params object.
   */
  notify(method: string, params?: object): void {
    if (this.closed) return
    this.write(params === undefined ? { jsonrpc: '2.0', method } : { jsonrpc: '2.0', method, params })
  }

  /**
   * Wait for prior frame writes to settle.
   * @returns a promise resolved by the output write callback.
   */
  flush(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.output.write('', (error) => {
        if (error !== null && error !== undefined) reject(error)
        else resolve()
      })
    })
  }

  private readonly onData = (chunk: Buffer | string): void => {
    if (this.closed) return
    const text = typeof chunk === 'string' ? chunk : this.decoder.write(chunk)
    this.buffer += text
    this.bufferedBytes += Buffer.byteLength(text, 'utf8')
    this.drainLines()
    if (!this.closed && this.buffer.indexOf('\n') < 0 && this.bufferedBytes > this.maxLineBytes) {
      this.failLineTooLong()
    }
  }

  private readonly onInputError = (error: Error): void => {
    this.failPending(error)
  }

  private readonly onInputEnd = (): void => {
    this.buffer += this.decoder.end()
    this.drainLines()
    this.failPending(new Error('JSON-RPC input closed'))
  }

  private drainLines(): void {
    for (;;) {
      if (this.closed) return
      const newline = this.buffer.indexOf('\n')
      if (newline < 0) return
      const raw = this.buffer.slice(0, newline)
      this.buffer = this.buffer.slice(newline + 1)
      this.bufferedBytes = Math.max(0, this.bufferedBytes - Buffer.byteLength(raw, 'utf8') - 1)
      const line = raw.trim()
      if (line === '') continue
      if (Buffer.byteLength(line, 'utf8') > this.maxLineBytes) {
        this.failLineTooLong()
        return
      }
      void this.handleLine(line)
    }
  }

  private async handleLine(line: string): Promise<void> {
    let message: unknown
    try {
      message = JSON.parse(line)
    } catch {
      this.writeError(null, -32700, 'parse error', { code: PROTOCOL_PARSE_FAILED })
      return
    }
    if (Array.isArray(message)) {
      this.writeError(null, -32600, 'batch requests are not supported', { code: INVALID_ARGUMENT })
      return
    }
    if (message === null || typeof message !== 'object') {
      this.writeError(null, -32600, 'invalid request', { code: INVALID_ARGUMENT })
      return
    }
    const frame = message as Record<string, unknown>
    const id = frame.id
    const method = frame.method
    if (typeof method === 'string' && (typeof id === 'string' || typeof id === 'number')) {
      await this.handleIncomingRequest(id, method, frame.params)
      return
    }
    if (typeof method === 'string') {
      try {
        this.notificationHandler?.(method, this.objectParams(frame.params))
      } catch {
        // Notifications have no response channel; malformed params are ignored.
      }
      return
    }
    if (typeof id === 'string' || typeof id === 'number') {
      this.handleIncomingResponse(id, frame)
      return
    }
    this.writeError(null, -32600, 'invalid request', { code: INVALID_ARGUMENT })
  }

  private async handleIncomingRequest(id: JsonRpcId, method: string, rawParams: unknown): Promise<void> {
    const handler = this.requestHandler
    if (handler === undefined) {
      this.writeError(id, -32601, `method not found: ${method}`, { code: METHOD_NOT_FOUND })
      return
    }
    try {
      const params = this.objectParams(rawParams)
      const result = await handler(method, params)
      this.write({ jsonrpc: '2.0', id, result })
    } catch (error) {
      this.writeHandlerError(id, error)
    }
  }

  private objectParams(raw: unknown): Record<string, unknown> {
    if (raw === undefined || raw === null) return {}
    if (typeof raw === 'object' && !Array.isArray(raw)) return raw as Record<string, unknown>
    throw new BoleRpcError(-32602, 'params must be an object', { code: INVALID_ARGUMENT })
  }

  private handleIncomingResponse(id: JsonRpcId, frame: Record<string, unknown>): void {
    const pending = this.pending.get(id)
    if (pending === undefined) return
    this.pending.delete(id)
    if (frame.error !== undefined && frame.error !== null && typeof frame.error === 'object') {
      const error = frame.error as Record<string, unknown>
      const failure = new Error(typeof error.message === 'string' ? error.message : 'JSON-RPC error')
      Object.assign(failure, { code: error.code, data: error.data })
      pending.reject(failure)
      return
    }
    pending.resolve(frame.result)
  }

  private writeHandlerError(id: JsonRpcId, error: unknown): void {
    if (error instanceof BoleRpcError) {
      this.writeError(id, error.code, error.message, error.data)
      return
    }
    const message = error instanceof Error ? error.message : String(error)
    this.writeError(id, -32603, message, { code: INTERNAL })
  }

  private writeError(id: JsonRpcId | null, code: number, message: string, data?: Record<string, unknown>): void {
    this.write({
      jsonrpc: '2.0',
      id,
      error: { code, message, ...data === undefined ? {} : { data } },
    })
  }

  private failLineTooLong(): void {
    if (this.closed) return
    const error = new BoleRpcError(-32600, 'line exceeds max_line_bytes', { code: LINE_TOO_LONG })
    this.writeError(null, error.code, error.message, error.data)
    this.close()
    this.onFatal?.(error)
  }

  private write(message: Record<string, unknown>): void {
    this.output.write(`${JSON.stringify(message)}\n`)
  }

  private failPending(error: Error): void {
    const pending = [...this.pending.values()]
    this.pending.clear()
    for (const waiter of pending) waiter.reject(error)
  }
}
