/**
 * Drive the C ABI end-to-end against the real `--profile sdk` runtime.
 *
 * Prerequisites (development only):
 *   - a runtime tree locked at 0.1.7-alpha.2 in BOLE_DEV_RUNTIME
 *   - `@bole/sdk-server` built and linked by runtime/dev
 *   - `node runtime/dev/build-experience.mjs` has run
 *
 * The script starts the deterministic mock model, assembles a single patch
 * (dev plugin + mock provider), creates an isolated DSH_HOME, and runs the C
 * test binary. It never ships; nothing here is copied into the offline package.
 *
 * Usage:
 *   BOLE_DEV_RUNTIME=<runtime> node native/scripts/integration-runtime.mjs \
 *     <path-to-bole_test_runtime>
 */

import { spawn } from 'node:child_process'
import { existsSync } from 'node:fs'
import { chmod, mkdir, mkdtemp, readFile, rm, symlink, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const repoRoot = resolve(here, '../..')
const binary = process.argv[2] ?? join(repoRoot, 'build/native/bole_test_runtime')
const devRuntime = process.env.BOLE_DEV_RUNTIME ?? join(process.env.HOME ?? '.', '.bole-dev/runtime')
const devPatch = join(repoRoot, 'runtime/dev/.dev/cordis.patch.yml')

if (!existsSync(binary)) {
  throw new Error(`missing C test binary: ${binary}`)
}
if (!existsSync(devPatch)) {
  throw new Error(`missing development patch: ${devPatch}; run runtime/dev build + ensureDevLinks`)
}

process.env.BOLE_DEV_RUNTIME = devRuntime
const dev = await import(pathToFileURL(join(repoRoot, 'runtime/dev/shared/runtime.mjs')).href)
const { startMockModel } = await import(
  pathToFileURL(join(repoRoot, 'runtime/dev/mock-model.mjs')).href
)

const mock = await startMockModel({ delayMs: 800 })
let home
let root
let child
try {
  home = await dev.prepareHome('bole-c-e2e-home-')
  await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')

  root = await mkdtemp(join(tmpdir(), 'bole-c-e2e-'))
  const runtimeDir = join(root, 'runtime')
  const cwd = join(root, 'work')
  await mkdir(runtimeDir, { recursive: true })
  await mkdir(cwd, { recursive: true })
  await symlink(join(devRuntime, 'node_modules'), join(runtimeDir, 'node_modules'), 'dir')

  const nodeExe = join(runtimeDir, 'node.exe')
  await writeFile(nodeExe, `#!/bin/sh\nexec "${process.execPath}" "$@"\n`)
  await chmod(nodeExe, 0o755)

  const devPatchText = (await readFile(devPatch, 'utf8')).trimEnd()
  const mockPatch = `- id: llm-pi-ai
  config:
    providers:
      bole:
        displayName: Bole Model
        api: openai-completions
        baseURL: ${mock.url}
        apiKeyEnv: BOLE_API_KEY
        models:
          - id: qwen3.8-27b
          - id: test-model
`
  await writeFile(join(runtimeDir, 'cordis.patch.yml'), `${devPatchText}\n${mockPatch}`)

  child = spawn(binary, [], {
    env: {
      ...process.env,
      BOLE_TEST_RUNTIME_DIR: runtimeDir,
      BOLE_TEST_DSH_HOME: home,
      BOLE_TEST_CWD: cwd,
    },
    stdio: 'inherit',
  })
  const exit = await new Promise((resolveExit) => {
    child.once('exit', (code, signal) => resolveExit({ code, signal }))
    child.once('error', (error) => resolveExit({ code: 1, signal: String(error) }))
  })
  if (exit.code !== 0) {
    console.error(`C runtime test failed: code=${String(exit.code)} signal=${String(exit.signal)}`)
    process.exitCode = 1
  } else {
    console.log(`PASS C ABI runtime integration (mock calls: ${mock.calls.length})`)
  }
} finally {
  if (child !== undefined && child.exitCode === null && child.signalCode === null) {
    child.kill('SIGKILL')
  }
  await mock.close()
  if (root !== undefined) {
    await rm(root, { recursive: true, force: true })
  }
  if (home !== undefined) {
    await rm(home, { recursive: true, force: true })
  }
}
