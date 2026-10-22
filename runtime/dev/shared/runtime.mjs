/**
 * Development-only helpers for driving the Bole SDK runtime.
 *
 * Nothing in `runtime/dev` ships in the offline package. Paths are resolved
 * from this file so the same scripts work from any checkout location.
 */

import { spawn } from 'node:child_process'
import { existsSync } from 'node:fs'
import { mkdir, mkdtemp, readFile, readlink, rm, symlink, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { createInterface } from 'node:readline'
import { fileURLToPath } from 'node:url'

/** Absolute `runtime/dev` directory. */
export const devDir = dirname(fileURLToPath(import.meta.url))
/** Absolute `runtime/dev` directory. */
export const devRoot = resolve(devDir, '..')
/** Repository root (`<repo>`). */
export const repoRoot = resolve(devDir, '../../..')
/** Runtime tree used for development; override with `BOLE_DEV_RUNTIME`. */
export const runtimeDir = process.env.BOLE_DEV_RUNTIME ?? join(process.env.HOME ?? '.', '.bole-dev/runtime')
/** Plugin package under development. */
export const pluginDir = join(repoRoot, 'runtime/packages/sdk-server')
/** Patch layer shipped by `@bole/sdk-app`. */
export const patchFile = join(repoRoot, 'runtime/packages/sdk-app/cordis.patch.yml')
/** dsh launcher inside the development runtime. */
export const cliBin = join(runtimeDir, 'node_modules/@deepseek-ai/dsh/lib/bin.js')
/** Generated development patch: the shipped patch with an absolute plugin entry. */
export const devPatchFile = join(devRoot, '.dev/cordis.patch.yml')

/**
 * Link the plugin's runtime dependencies and the plugin itself into the
 * development runtime. Idempotent; safe to run on every smoke.
 * @returns the plugin link path created in the runtime.
 */
export async function ensureDevLinks() {
  if (!existsSync(cliBin)) {
    throw new Error(`dsh runtime not found at ${runtimeDir}; set BOLE_DEV_RUNTIME or install @deepseek-ai/dsh@0.1.7-alpha.2`)
  }
  const pluginNodeModules = join(pluginDir, 'node_modules')
  await mkdir(pluginNodeModules, { recursive: true })
  await linkDir(join(runtimeDir, 'node_modules/@deepseek-ai'), join(pluginNodeModules, '@deepseek-ai'))
  await linkDir(join(devRoot, 'node_modules/@types'), join(pluginNodeModules, '@types'))

  const scopeDir = join(runtimeDir, 'node_modules/@bole')
  await mkdir(scopeDir, { recursive: true })
  const pluginLink = join(scopeDir, 'sdk-server')
  await linkDir(pluginDir, pluginLink)
  await ensureDevPatch()
  return pluginLink
}

/**
 * Generate the development patch by pointing the shipped plugin row at the
 * built entry. Bare package names in a patch resolve beside the patch file, so
 * the delivery patch cannot find a dev-only package by name.
 * @returns the generated patch path.
 */
export async function ensureDevPatch() {
  const source = await readFile(patchFile, 'utf8')
  const entry = join(pluginDir, 'lib/index.js').replaceAll("'", "''")
  await mkdir(dirname(devPatchFile), { recursive: true })
  await writeFile(devPatchFile, source.replace("name: '@bole/sdk-server'", `name: '${entry}'`))
  return devPatchFile
}

/**
 * Replace one directory symlink when its target differs.
 * @param target - existing directory the link should point at.
 * @param linkPath - symlink to create or replace.
 */
async function linkDir(target, linkPath) {
  try {
    if (await readlink(linkPath) === target) return
  } catch {
    // Missing link (or a stale non-link file): fall through and recreate.
  }
  await rm(linkPath, { force: true, recursive: false })
  await symlink(target, linkPath, 'dir')
}

/**
 * Create an isolated DSH_HOME for one run.
 * @param prefix - temporary directory prefix.
 * @returns the created temporary home.
 */
export async function makeHome(prefix = 'bole-smoke-') {
  return mkdtemp(join(tmpdir(), prefix))
}

/**
 * Spawn the sdk profile with an isolated home and the shipped patch layer.
 * @param options - home directory, patch layers, cwd, and extra environment.
 * @returns the child process with piped stdio.
 */
export function spawnRuntime({ home, patches = [devPatchFile], cwd = repoRoot, env = {} } = {}) {
  if (home === undefined) throw new Error('spawnRuntime requires a temporary DSH_HOME')
  const args = [cliBin, '--profile', 'sdk', ...patches.flatMap((file) => ['--patch', file])]
  return spawn(process.execPath, args, {
    cwd,
    env: {
      ...process.env,
      DSH_HOME: home,
      DSH_TELEMETRY_DISABLED: '1',
      DSH_PERMISSION_MODE: 'danger-full-access',
      NO_COLOR: '1',
      ...env,
    },
    stdio: ['pipe', 'pipe', 'pipe'],
  })
}

/** Minimal newline-delimited JSON-RPC client for the SDK runtime. */
export class RpcClient {
  /** Unparsed stdout lines, in arrival order. */
  lines = []
  /** Parsed notifications, in arrival order. */
  notifications = []
  /** Parsed JSON-RPC frames, in arrival order. */
  frames = []
  /** Raw stderr text. */
  stderr = ''
  /** First parsed stdout frame; resolves on the first JSON-RPC object. */
  firstFrame

  #child
  #nextId = 1
  #pending = new Map()
  #stderrLines = []
  #resolveFirst
  #exit

  /**
   * @param child - process returned by {@link spawnRuntime}.
   */
  constructor(child) {
    this.#child = child
    this.firstFrame = new Promise((resolveFrame) => { this.#resolveFirst = resolveFrame })
    this.#exit = new Promise((resolveExit) => {
      child.once('exit', (code, signal) => resolveExit({ code, signal }))
    })
    createInterface({ input: child.stdout }).on('line', (line) => { this.#onLine(line) })
    createInterface({ input: child.stderr }).on('line', (line) => {
      this.#stderrLines.push(line)
      this.stderr += `${line}\n`
    })
  }

  #onLine(line) {
    const trimmed = line.trim()
    if (trimmed === '') return
    this.lines.push(trimmed)
    let frame
    try {
      frame = JSON.parse(trimmed)
    } catch {
      return
    }
    if (frame && typeof frame === 'object') this.frames.push(frame)
    if (frame && typeof frame === 'object' && frame.jsonrpc === '2.0') this.#resolveFirst(frame)
    if (frame && typeof frame === 'object' && frame.method !== undefined) {
      this.notifications.push(frame)
      return
    }
    if (frame && typeof frame === 'object' && frame.id !== undefined) {
      const pending = this.#pending.get(frame.id)
      if (pending === undefined) return
      this.#pending.delete(frame.id)
      clearTimeout(pending.timer)
      if (frame.error !== undefined) {
        const error = new Error(frame.error.message ?? 'JSON-RPC error')
        error.rpc = frame.error
        pending.reject(error)
      } else {
        pending.resolve(frame.result)
      }
    }
  }

  /**
   * Send one request and await its result.
   * @param method - JSON-RPC method.
   * @param params - request parameters.
   * @param timeoutMs - rejection timeout.
   * @returns the JSON-RPC result.
   */
  request(method, params = {}, timeoutMs = 60_000) {
    const id = this.#nextId++
    return new Promise((resolveRequest, reject) => {
      const timer = setTimeout(() => {
        this.#pending.delete(id)
        reject(new Error(`JSON-RPC timeout: ${method}`))
      }, timeoutMs)
      this.#pending.set(id, { resolve: resolveRequest, reject, timer })
      this.#child.stdin.write(`${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`)
    })
  }

  /** Wait for the runtime process to exit. */
  waitExit(timeoutMs = 20_000) {
    return Promise.race([
      this.#exit,
      new Promise((_, reject) => setTimeout(() => reject(new Error('runtime did not exit')), timeoutMs)),
    ])
  }

  /**
   * Write one raw protocol line (used for malformed-input coverage).
   * @param line - line content without the trailing newline.
   */
  sendRaw(line) {
    this.#child.stdin.write(`${line}\n`)
  }

  /** Close stdin without waiting for a shutdown response. */
  closeInput() {
    this.#child.stdin.end()
  }

  /** Last stderr lines, useful in failure messages. */
  stderrTail(count = 20) {
    return this.#stderrLines.slice(-count).join('\n')
  }
}

/**
 * Assert a condition with a diagnostic suffix.
 * @param condition - value that must be truthy.
 * @param message - failure message.
 */
export function assert(condition, message) {
  if (!condition) throw new Error(message)
}

/** Remove a temporary home, ignoring cleanup failures. */
export async function cleanup(home) {
  if (home !== undefined) await rm(home, { recursive: true, force: true }).catch(() => {})
}
