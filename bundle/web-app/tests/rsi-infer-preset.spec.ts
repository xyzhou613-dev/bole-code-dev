/** The fifth preset differs from the no-experience Standard only by the Bole plugin. */
import { readFile } from 'node:fs/promises'
import { load } from 'js-yaml'
import { entryListSchema, type PatchOptions } from '@deepseek-ai/cordis-plugin-include'
import { expect, it } from 'vitest'
import type { PresetDefinition } from '@deepseek-ai/dsh-agent-preset-registry'

async function declaration(id: string): Promise<PresetDefinition> {
  const text = await readFile(new URL(`../presets/${id}.patch.yml`, import.meta.url), 'utf8')
  const patches = load(text, { schema: entryListSchema }) as PatchOptions[]
  const row = patches[0]?.insert?.[0]
  if (row === undefined) throw new Error('Missing shipped preset declaration')
  return row.config as PresetDefinition
}

it('preserves all Standard plugins and adds only read-only experience as the fifth mode', async () => {
  const standard = await declaration('standard')
  const rsi = await declaration('rsi-infer')
  expect(rsi.id).toBe('rsi-infer')
  expect(rsi.order).toBe(5)
  expect(rsi.plugins.slice(0, -1)).toEqual(standard.plugins)
  expect(rsi.plugins.at(-1)).toEqual({
    id: 'bole-experience', name: '@deepseek-ai/dsh-skill-bole-experience', config: { maxContextBytes: 4096 },
  })
  const manifest = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8')) as {
    dsh: { bundle: { patch: string[] } }
    files: string[]
    dependencies: Record<string, string>
  }
  expect(manifest.dsh.bundle.patch).toEqual([
    './cordis.patch.yml', './presets/standard.patch.yml', './presets/ptc.patch.yml',
    './presets/minimal.patch.yml', './presets/cordis.patch.yml', './presets/rsi-infer.patch.yml',
  ])
  expect(manifest.files).toContain('presets/rsi-infer.patch.yml')
  expect(manifest.dependencies['@deepseek-ai/dsh-skill-bole-experience']).toBe('workspace:*')
  for (const id of ['standard', 'ptc', 'minimal', 'cordis']) {
    expect(JSON.stringify((await declaration(id)).plugins)).not.toContain('skill-bole-experience')
  }
})
