/**
 * Build the Bole experience plugin for the development runtime.
 *
 * The package is not published to npm and its source tsconfig references a
 * repository-root base that this delivery checkout does not carry. Bundle it
 * with esbuild, keep the official packages external, and install the result as
 * a real directory so Node resolves its peers from the runtime tree.
 *
 * Usage: node runtime/dev/build-experience.mjs
 */

import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { build } from 'esbuild'
import { devRoot, repoRoot, runtimeDir } from './shared/runtime.mjs'

const source = join(repoRoot, 'skill/skill-bole-experience')
const output = join(devRoot, '../dist/skill-bole-experience')
const packageJson = JSON.parse(await readFile(join(source, 'package.json'), 'utf8'))

await rm(output, { recursive: true, force: true })
await mkdir(join(output, 'lib'), { recursive: true })
await build({
  entryPoints: [join(source, 'src/index.ts')],
  outfile: join(output, 'lib/index.js'),
  bundle: true,
  platform: 'node',
  format: 'esm',
  target: 'node22',
  external: ['@deepseek-ai/*'],
  nodePaths: [join(runtimeDir, 'node_modules')],
  logLevel: 'error',
})
await cp(join(source, 'assets'), join(output, 'assets'), { recursive: true })
await writeFile(join(output, 'package.json'), `${JSON.stringify({
  name: packageJson.name,
  version: packageJson.version,
  private: true,
  type: 'module',
  main: 'lib/index.js',
  exports: { '.': './lib/index.js' },
}, null, 2)}\n`)

const installed = join(runtimeDir, 'node_modules/@deepseek-ai/dsh-skill-bole-experience')
await rm(installed, { recursive: true, force: true })
await cp(output, installed, { recursive: true })
console.log(`built and installed ${packageJson.name} -> ${installed}`)
