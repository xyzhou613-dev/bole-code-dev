/**
 * C2/M0 evidence: console-c + real runtime + toy MCP + mock model.
 *
 * Development-only. Requires:
 *   - BOLE_DEV_RUNTIME pointing at a 0.1.7-alpha.2 runtime tree
 *   - the bridge plugin built and linked by runtime/dev
 *   - the experience package installed by runtime/dev/build-experience.mjs
 *
 * Usage:
 *   BOLE_DEV_RUNTIME=<runtime> node examples/console-c/smoke.mjs <console-binary>
 */

import { spawn } from 'node:child_process'
import { existsSync } from 'node:fs'
import { chmod, mkdir, mkdtemp, readFile, rm, symlink, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const repoRoot = resolve(here, '../..')
const binary = process.argv[2] ?? join(repoRoot, 'build/console/bole_console')
const devRuntime = process.env.BOLE_DEV_RUNTIME ?? join(process.env.HOME ?? '.', '.bole-dev/runtime')
const devPatch = join(repoRoot, 'runtime/dev/.dev/cordis.patch.yml')

if (!existsSync(binary)) throw new Error(`missing console binary: ${binary}`)
if (!existsSync(devPatch)) throw new Error(`missing development patch: ${devPatch}`)

process.env.BOLE_DEV_RUNTIME = devRuntime
process.env.MOCK_TOOL = 'mcp__toy__add'
process.env.MOCK_TOOL_ARGS = '{"a":1,"b":2}'

const dev = await import(pathToFileURL(join(repoRoot, 'runtime/dev/shared/runtime.mjs')).href)
const { startMockModel } = await import(
  pathToFileURL(join(repoRoot, 'runtime/dev/mock-model.mjs')).href
)

const quote = (value) => `'${value.replaceAll("'", "''")}'`
const mock = await startMockModel()
let home
let root
let child
try {
  home = await dev.prepareHome('bole-console-home-')
  await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')

  root = await mkdtemp(join(tmpdir(), 'bole-console-'))
  const runtimeDir = join(root, 'runtime')
  await mkdir(runtimeDir, { recursive: true })
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
`
  const mcpPatch = `- insert:
    - id: mcp-toy
      name: '@deepseek-ai/dsh-mcp-client'
      config:
        transport: stdio
        serverName: toy
        command: ${quote(nodeExe)}
        args: [${quote(join(repoRoot, 'examples/toy-mcp/server.mjs'))}]
        cwd: ${quote(repoRoot)}
        toolCallTimeoutMs: 10000
        failOnStartupError: false
`
  await writeFile(join(runtimeDir, 'cordis.patch.yml'),
    `${devPatchText}\n${mockPatch}${mcpPatch}`)

  const prompt = '用玩具工具计算 1+2'
  const started = Date.now()
  child = spawn(binary, [runtimeDir, home, prompt], {
    cwd: repoRoot,
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  let stdout = ''
  let stderr = ''
  child.stdout.on('data', (chunk) => { stdout += chunk })
  child.stderr.on('data', (chunk) => { stderr += chunk })
  const timeout = setTimeout(() => child.kill('SIGKILL'), 90_000)
  const exit = await new Promise((resolveExit) => {
    child.once('exit', (code, signal) => resolveExit({ code, signal }))
    child.once('error', (error) => resolveExit({ code: 1, signal: String(error) }))
  })
  clearTimeout(timeout)
  const elapsed = Date.now() - started

  const checks = [
    ['tool started', stdout.includes('mcp__toy__add')],
    ['tool finished', stdout.includes('[tool] done') && stdout.includes('"isError":false')],
    ['task finished', stdout.includes('[task] finished')],
    ['exit code', exit.code === 0],
  ]
  for (const [name, ok] of checks) {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${name}`)
  }
  console.log(`TIMING ${elapsed} ms`)
  console.log(`MOCK_CALLS ${mock.calls.length}`)
  if (checks.some(([, ok]) => !ok)) {
    console.error('--- stdout ---\n' + stdout)
    console.error('--- stderr ---\n' + stderr)
    process.exitCode = 1
  } else {
    console.log('PASS console-c C2/M0 tool call')
  }
} finally {
  if (child !== undefined && child.exitCode === null && child.signalCode === null) child.kill('SIGKILL')
  await mock.close()
  if (root !== undefined) await rm(root, { recursive: true, force: true })
  if (home !== undefined) await rm(home, { recursive: true, force: true })
}
