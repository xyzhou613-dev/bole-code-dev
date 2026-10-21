/** Immutable final-experience assets and exact model-id selection. */
import { createHash } from 'node:crypto'
import { readFile } from 'node:fs/promises'
import { z } from 'zod'

const bundleSchema = z.array(z.object({
  id: z.enum(['qwen38-service-v3', 'ornith-v18']),
  models: z.array(z.string().min(1)).min(1),
  originModel: z.string().min(1),
  originDeployment: z.string().min(1),
  evaluation: z.object({
    dataset: z.literal('terminal-bench-2.1'), runId: z.string().min(1),
    passed: z.number().int().nonnegative(), total: z.literal(89), errors: z.number().int().nonnegative(),
  }).strict(),
  skill: z.object({
    id: z.string().regex(/^[a-z0-9]+(?:-[a-z0-9]+)*$/),
    name: z.string().regex(/^bole-[a-z0-9]+(?:-[a-z0-9]+)*$/),
    title: z.string().min(1), description: z.string().min(1), whenToUse: z.string().min(1),
    body: z.string().min(1), sha256: z.string().regex(/^[0-9a-f]{64}$/),
  }).strict(),
}).strict()).length(2)

/** One approved model's selected experience and historical evaluation identity. */
export type ExperienceBundle = z.infer<typeof bundleSchema>[number]

/**
 * Load the packaged assets, rejecting duplicate identities and changed bodies.
 * @returns validated final experiences; no live memory store is read or written.
 */
export async function loadBundles(): Promise<readonly ExperienceBundle[]> {
  const bundles = bundleSchema.parse(JSON.parse(await readFile(new URL('../assets/final-experiences.json', import.meta.url), 'utf8')))
  const ids = new Set<string>()
  const models = new Set<string>()
  for (const bundle of bundles) {
    if (ids.has(bundle.id)) throw new Error(`duplicate Bole experience bundle: ${bundle.id}`)
    ids.add(bundle.id)
    for (const model of bundle.models) {
      if (models.has(model)) throw new Error(`duplicate Bole experience model: ${model}`)
      models.add(model)
    }
    if (createHash('sha256').update(bundle.skill.body).digest('hex') !== bundle.skill.sha256) {
      throw new Error(`Bole experience body checksum mismatch: ${bundle.skill.id}`)
    }
  }
  return bundles
}

/**
 * Match public service-lifecycle requests, not a benchmark task allowlist.
 * @param text - user-authored public task text, excluding test feedback and model output.
 * @returns whether the task is eligible for automatic service experience delivery.
 */
export function matchesServiceTask(text: string): boolean {
  return /\b(?:start|launch|run|host|serve|deploy|leave|keep)\b[^.\n]{0,120}\b(?:server|service|daemon|listener|api)\b/i.test(text)
    || /\b(?:server|service|daemon|listener|api)\b[^.\n]{0,120}\b(?:running|listening|reachable|after|remain|outlive)\b/i.test(text)
    || /(?:启动|部署|运行|搭建|保持|留下)[^。\n]{0,60}(?:服务器|服务|监听|API)/i.test(text)
    || /(?:服务器|服务|监听)[^。\n]{0,60}(?:保持|运行|退出后|结束后)/i.test(text)
}
