/**
 * A8 clean-install check: extract the packed plugin/experience tarballs into an
 * SDK profile and boot with a patch that references `@bole/sdk-server` by name.
 *
 * Usage: node runtime/dev/pack.mjs && node runtime/dev/pack-test.mjs
 */

import { execFileSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import { readFile, rm, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import {
  assert,
  cleanup,
  devPatchFile,
  prepareHome,
  RpcClient,
  repoRoot,
  spawnRuntime,
} from './shared/runtime.mjs'

const pluginTarball = join(repoRoot, 'runtime/dist/bole-sdk-server-0.1.0.tgz')
const experienceTarball = join(repoRoot, 'runtime/dist/deepseek-ai-dsh-skill-bole-experience-0.1.7-alpha.2.tgz')
for (const file of [pluginTarball, experienceTarball]) {
  if (!existsSync(file)) throw new Error(`missing tarball ${file}; run node runtime/dev/pack.mjs first`)
}

const home = await prepareHome('bole-pack-')
await writeFile(join(home, '.env'), 'BOLE_API_KEY=local\n')
const profile = join(home, 'profiles/sdk')
const profileModules = join(profile, 'node_modules')

/** Extract one npm tarball into a package directory. */
function extract(tarball, destination) {
  execFileSync('mkdir', ['-p', destination])
  execFileSync('tar', ['-xzf', tarball, '-C', destination, '--strip-components=1'])
}

await rm(join(profileModules, '@bole/sdk-server'), { recursive: true, force: true })
await rm(join(profileModules, '@deepseek-ai/dsh-skill-bole-experience'), { recursive: true, force: true })
extract(pluginTarball, join(profileModules, '@bole/sdk-server'))
extract(experienceTarball, join(profileModules, '@deepseek-ai/dsh-skill-bole-experience'))

const source = await readFile(devPatchFile, 'utf8')
const barePatch = join(home, 'bare.patch.yml')
await writeFile(barePatch, source.replace(/name: '[^']*\/runtime\/packages\/sdk-server\/lib\/index\.js'/, "name: '@bole/sdk-server'"))

let client
try {
  client = new RpcClient(spawnRuntime({ home, patches: [barePatch] }))
  const init = await client.request('initialize', { cwd: repoRoot, provider: 'bole', model: 'qwen3.8-27b' })
  assert(init?.capabilities?.preset === true, `packed runtime has no preset plane: ${JSON.stringify(init)}`)
  const created = await client.request('session/create', { sessionId: 's-packed' })
  assert(created.preset === 'standard', `packed runtime could not create a standard session: ${JSON.stringify(created)}`)
  assert(client.stderr.includes('[bole-sdk-server] loaded'), `packed plugin did not load:\n${client.stderrTail()}`)
  assert(!client.stderr.includes('did not activate'), `packed plugin did not activate:\n${client.stderrTail()}`)
  await client.request('shutdown', {}, 15_000)
  const exit = await client.waitExit(20_000)
  assert(exit.code === 0, `packed runtime exit ${String(exit.code)}`)
  console.log('PASS packed plugin resolves by bare name')
  console.log('PASS packed experience mounts in the preset plane')
  console.log('PASS packed runtime initialize/create/shutdown')
} catch (error) {
  console.error(`FAIL pack-test: ${error instanceof Error ? error.message : String(error)}`)
  if (client !== undefined) console.error(client.stderrTail(40))
  process.exitCode = 1
} finally {
  client?.closeInput()
  await cleanup(home)
}
