/**
 * A8 packaging inputs: build the plugin, pack it and the experience plugin, and
 * print the tarball paths for the offline-package assembler.
 *
 * Usage: node runtime/dev/pack.mjs
 */

import { execFileSync } from 'node:child_process'
import { mkdir } from 'node:fs/promises'
import { join } from 'node:path'
import { pluginDir, repoRoot } from './shared/runtime.mjs'

const output = join(repoRoot, 'runtime/dist')
await mkdir(output, { recursive: true })

const plugin = execFileSync('npm', ['pack', '--pack-destination', output], {
  cwd: pluginDir,
  encoding: 'utf8',
}).trim().split('\n').at(-1)

const experienceSource = join(output, 'skill-bole-experience')
const experience = execFileSync('npm', ['pack', '--pack-destination', output], {
  cwd: experienceSource,
  encoding: 'utf8',
}).trim().split('\n').at(-1)

console.log(`plugin: ${plugin}`)
console.log(`experience: ${experience}`)
console.log(`tarballs are written under ${output}`)
