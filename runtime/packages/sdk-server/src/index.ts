/**
 * Bole SDK JSON-RPC bridge.
 *
 * Stage 2: own the stdio transport with the frozen protocol's framing and
 * error rules, and serve the session methods through the in-package server.
 * Event projection, RSI, and trace are the next steps; see
 * docs/bole-sdk/09-a-track-plan.md.
 */

import type { Context } from '@deepseek-ai/cordis'
import type {} from '@deepseek-ai/cordis-plugin-loader'
import type {} from '@deepseek-ai/dsh-agent'
import type { Readable, Writable } from 'node:stream'
import { BoleSdkServer } from './server.js'
import { BoleLineTransport } from './transport.js'

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
  const server = new BoleSdkServer(ctx, transport)

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
    // A protocol probe used by the development smoke; removed once fixtures
    // exercise the session methods directly.
    if (method === 'bole/echo') return { echo: params }
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
