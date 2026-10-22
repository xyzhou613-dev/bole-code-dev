/**
 * Bole SDK JSON-RPC bridge.
 *
 * Stage 0: replace the official `sdk-jsonrpc-server` row and serve the official
 * three methods through the same public server class. Custom Bole methods and
 * event projection are added in later stages.
 */

import type { Context } from '@deepseek-ai/cordis'
import type {} from '@deepseek-ai/cordis-plugin-loader'
import type {} from '@deepseek-ai/dsh-agent'
import type { Readable, Writable } from 'node:stream'
import { JsonRpcLineTransport } from '@deepseek-ai/dsh-sdk-protocol'
import { HarnessSdkJsonRpcServer } from '@deepseek-ai/dsh-sdk-jsonrpc-server'

export const name = 'bole-sdk-server'

/** The agent factory is required; the loader seam is optional for readiness. */
export const inject = ['agents']

/** Runtime-only test hooks; production uses process stdio. */
export interface Config {
  input?: Readable
  output?: Writable
  exit?: (code: number) => void
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

  const transport = new JsonRpcLineTransport(input, output)
  const server = new HarnessSdkJsonRpcServer(ctx, transport, { maxTokensAsSuccess: true })

  let exitTask: Promise<void> | undefined
  const disposeAndExit = (): Promise<void> => {
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
    const result = await server.handleRequest(method, params)
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
