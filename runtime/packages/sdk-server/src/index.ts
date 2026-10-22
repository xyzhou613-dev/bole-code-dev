/**
 * Bole SDK JSON-RPC bridge.
 *
 * Stage 1: own the stdio transport with the frozen protocol's framing and
 * error rules, and dispatch Bole methods before delegating the official
 * `initialize` / `session/prompt` / `shutdown` methods to the official server.
 * The session-aware server (create/resume/cancel/setPreset/trace/RSI) is the
 * next step; see docs/bole-sdk/09-a-track-plan.md.
 */

import type { Context } from '@deepseek-ai/cordis'
import type {} from '@deepseek-ai/cordis-plugin-loader'
import type {} from '@deepseek-ai/dsh-agent'
import type { Readable, Writable } from 'node:stream'
import { HarnessSdkJsonRpcServer } from '@deepseek-ai/dsh-sdk-jsonrpc-server'
import { BoleLineTransport, BoleRpcError } from './transport.js'

export const name = 'bole-sdk-server'

/** The agent factory is required; the loader seam is optional for readiness. */
export const inject = ['agents']

/** Runtime-only test hooks; production uses process stdio. */
export interface Config {
  input?: Readable
  output?: Writable
  exit?: (code: number) => void
  /** Maximum UTF-8 bytes per JSON-RPC frame; defaults to 64 MiB. */
  maxLineBytes?: number
}

const OFFICIAL_METHODS = new Set(['initialize', 'session/prompt', 'shutdown'])

/** The runtime facts advertised by `initialize`; 02-protocol.md §2. */
interface SdkCapabilities {
  preset: boolean
  sandbox: 'workspace-write' | 'danger-full-access' | 'unavailable'
  persistence: boolean
  trace: boolean
}

/**
 * Read capability facts from the services actually mounted in this profile.
 * @param ctx - the profile context.
 * @returns the capability block for the initialize result.
 */
function capabilitiesOf(ctx: Context): SdkCapabilities {
  const mode = ctx.get('sandboxPolicy')?.defaultMode
  return {
    preset: ctx.get('agentPresets') !== undefined,
    sandbox: mode === 'workspace-write' || mode === 'danger-full-access' ? mode : 'unavailable',
    persistence: ctx.get('sessionPersistence') !== undefined,
    trace: ctx.get('sessionQuery') !== undefined,
  }
}

/**
 * Serve the Bole SDK protocol over the configured streams.
 *
 * @param ctx - the profile context.
 * @param config - optional stream and exit overrides.
 */
export function apply(ctx: Context, config: Config = {}): void {
  process.stderr.write('[bole-sdk-server] loaded\n')
  const rootFiber = ctx.root.fiber
  const input = config.input ?? process.stdin
  const output = config.output ?? process.stdout
  const exit = config.exit ?? ((code: number): void => { process.exit(code) })

  let exitTask: Promise<void> | undefined

  const transport = new BoleLineTransport(input, output, {
    ...(config.maxLineBytes === undefined ? {} : { maxLineBytes: config.maxLineBytes }),
    onFatal: () => { setImmediate(() => { void disposeAndExit() }) },
  })
  const server = new HarnessSdkJsonRpcServer(ctx, transport, { maxTokensAsSuccess: true })

  function disposeAndExit(): Promise<void> {
    exitTask ??= (async () => {
      await Promise.allSettled([Promise.resolve().then(() => transport.flush())])
      await Promise.allSettled([Promise.resolve().then(() => rootFiber.dispose())])
      exit(0)
    })()
    return exitTask
  }

  transport.onRequest(async (method, params) => {
    if (method === 'initialize') {
      await ctx.get('loader')?.await()
    }
    // A protocol probe used by the A1 spike; replaced by the session methods.
    if (method === 'bole/echo') return { echo: params }
    if (!OFFICIAL_METHODS.has(method)) {
      throw new BoleRpcError(-32601, `method not found: ${method}`, { code: 'METHOD_NOT_FOUND' })
    }
    const result = await server.handleRequest(method, params)
    if (method === 'initialize') {
      return {
        ...(result as Record<string, unknown>),
        sdkProtocolVersion: 1,
        capabilities: capabilitiesOf(ctx),
      }
    }
    if (method === 'shutdown') {
      setImmediate(() => { void disposeAndExit() })
    }
    return result
  })

  ctx.effect(() => {
    transport.start()
    return async () => {
      await server.shutdown()
      transport.close()
    }
  }, 'bole-sdk-server')
}
