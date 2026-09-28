/** Read-only final experiences, scoped to RSI-infer Agents and their exact model route. */
import type { Context } from '@deepseek-ai/cordis'
import z from '@deepseek-ai/schemastery'
import type { Agent } from '@deepseek-ai/dsh-agent'
import { createUserMessage } from '@deepseek-ai/dsh-llm'
import type { UserMessage } from '@deepseek-ai/dsh-session'
import { createScope } from '@deepseek-ai/dsh-scope'
import { BUNDLED_SKILL_RANK, escapeText, type SkillCandidate, type SkillDefinition } from '@deepseek-ai/dsh-skill'
import type {} from '@deepseek-ai/dsh-system-prompt'
import { loadBundles, matchesServiceTask, type ExperienceBundle } from './bundles.ts'

/** Configuration for the bounded, read-only experience delivery. */
export interface Config {
  /** Maximum UTF-8 bytes in a complete automatic context, including framing. */
  maxContextBytes: number
}

/** Runtime validation for the experience plugin. */
export const Config: z<Config> = z.object({ maxContextBytes: z.number().min(1).step(1).default(4096) })
/** Cordis plugin name. */
export const name = 'skill-bole-experience'
/** Required agent attribution, registry and prompt assembly services. */
export const inject = ['agents', 'skills', 'systemPrompt']

/** Durable selection and delivery receipt; historical source deployment is not the current route. */
export interface BoleExperienceSource {
  readonly kind: 'bole-experience'
  readonly form: 'instructions'
  readonly provider: string
  readonly model: string
  readonly bundleId: string | null
  readonly deliveredMemoryIds: readonly string[]
  readonly bodySha256: string | null
  readonly status: 'available' | 'delivered' | 'unsupported-model'
}

declare module '@deepseek-ai/dsh-llm' {
  interface MessageSourceMap {
    /**
     * Producer-owned delivery attribution. Readers without this plugin retain
     * the JSON metadata and derive the recorded content without an interpreter.
     * Only this producer consults receipts for duplicate and route checks.
     * @persistenceAttribution
     */
    'bole-experience': BoleExperienceSource
  }
}

interface Selection {
  readonly provider: string
  readonly model: string
  readonly bundle: ExperienceBundle | undefined
  readonly dispose: () => Promise<void>
}

function receipts(agent: Agent): BoleExperienceSource[] {
  return agent.session.deriveMessages().flatMap(message =>
    message.source.kind === 'bole-experience' ? [message.source] : [])
}

function assertSameRoute(source: Pick<BoleExperienceSource, 'provider' | 'model'>, provider: string, model: string): void {
  if (source.provider !== provider || source.model !== model) {
    throw new Error('RSI-infer experience is model-route bound. Start a new task to change provider or model; existing experience must not reach another model.')
  }
}

function definition(bundle: ExperienceBundle): SkillDefinition {
  return {
    name: bundle.skill.name,
    description: bundle.skill.description,
    whenToUse: bundle.skill.whenToUse,
    content: bundle.skill.body,
    invocation: { modelInvocable: true, userInvocable: true },
    provider: 'bole-final',
    source: 'bundled',
  }
}

function taskText(messages: readonly UserMessage[]): string {
  return messages.filter(message => message.source.kind === 'user')
    .flatMap(message => message.content.flatMap(block => block.type === 'text' ? [block.text] : []))
    .join('\n')
}

/**
 * Register exact-model skills and durable, task-relevant automatic delivery.
 * The actual outbound route is checked before request admission. A session with
 * experience cannot change model routes; use a fresh task for comparisons.
 * @param ctx - RSI-infer preset context; disposal removes every owned provider.
 * @param config - complete automatic-context byte bound.
 * @returns fulfillment after immutable assets validate and listeners register.
 */
export async function apply(ctx: Context, config: Config): Promise<void> {
  const bundles = await loadBundles()
  const selections = new Map<Agent, Selection>()
  const remove = async (agent: Agent): Promise<void> => {
    const selected = selections.get(agent)
    selections.delete(agent)
    await selected?.dispose()
  }
  ctx.effect(() => async () => { await Promise.all([...selections.keys()].map(remove)) }, 'bole-experience.providers()')
  ctx.on('agent/disposed', ({ agent }) => { selections.delete(agent) })

  // Assembly resolves the user-selected route before the skill catalog is read.
  ctx.on('system-prompt/assemble', async (_assembly, context, next) => {
    const assembled = await next()
    if (context.agent === undefined) return assembled
    const agent = context.agent
    const { provider, model } = assembled.variables
    if (typeof provider !== 'string' || typeof model !== 'string') {
      throw new Error('RSI-infer requires provider and model prompt variables before experience selection.')
    }
    for (const receipt of receipts(agent)) assertSameRoute(receipt, provider, model)
    // Compaction may replace receipt-bearing messages, but not the logged route.
    const priorRoute = agent.session.requestContext()
    if (priorRoute !== undefined) assertSameRoute(priorRoute, provider, model)
    const old = selections.get(agent)
    if (old?.provider === provider && old.model === model) return assembled
    await remove(agent)
    context.signal?.throwIfAborted()
    const bundle = bundles.find(item => item.models.includes(model))
    const skill = bundle === undefined ? undefined : definition(bundle)
    // Denied rows shadow any parent-agent experience with the same skill name.
    // Both the catalog and direct skill execution must enforce model isolation.
    const candidates: SkillCandidate[] = bundles.map(item => ({
      name: item.skill.name,
      description: item === bundle ? item.skill.description : 'This experience is unavailable for the current model.',
      invocation: { modelInvocable: item === bundle, userInvocable: item === bundle },
      provider: 'bole-final',
      source: 'bundled', rank: BUNDLED_SKILL_RANK, locator: item.id,
    }))
    const contribution = createScope(ctx, agent)
    contribution.ctx.skills.registerProvider(() => ({
      name: 'bole-final',
      list: () => Promise.resolve(candidates),
      get: candidate => Promise.resolve(candidate.name === skill?.name ? skill : undefined),
    }))
    // Agent teardown and plugin unload both await the scope's shared disposal.
    const lifetime = ctx.effect(() => agent.ctx.effect(
      () => () => contribution.dispose(), 'bole-experience.agent-provider()',
    ), 'bole-experience.owned-provider()')
    selections.set(agent, { provider, model, bundle, dispose: async () => {
      await lifetime()
      await contribution.dispose()
    } })
    return assembled
  }, { prepend: true })

  ctx.on('agent/pre-step', async ({ agent, signal, step }, next) => {
    const decision = await next()
    if (decision.kind === 'reject' || (step === 1 && decision.messages.length === 0)) return decision
    signal.throwIfAborted()
    const selected = selections.get(agent)
    if (selected === undefined) throw new Error('RSI-infer experience selection did not run during prompt assembly.')
    const previous = receipts(agent)
    const { bundle, provider, model } = selected
    if (previous.some(source => source.status === 'delivered')) return decision
    const relevant = bundle !== undefined && matchesServiceTask(taskText(decision.messages))
    if (!relevant && previous.length > 0) return decision
    const text = relevant
      ? ['<bole-experience>', `Use this experience only when applicable: ${escapeText(bundle.skill.whenToUse)}`,
        escapeText(bundle.skill.body), '</bole-experience>'].join('\n')
      : bundle === undefined
        ? 'No approved service-lifecycle experience is available for this model. Follow the task instructions.'
        : `The model-specific skill ${bundle.skill.name} is available. Load it only for tasks matching its description.`
    const source: BoleExperienceSource = {
      kind: 'bole-experience', form: 'instructions', provider, model,
      bundleId: bundle?.id ?? null,
      deliveredMemoryIds: relevant ? [bundle.skill.id] : [],
      bodySha256: relevant ? bundle.skill.sha256 : null,
      status: relevant ? 'delivered' : bundle === undefined ? 'unsupported-model' : 'available',
    }
    const message = createUserMessage({ content: [{ type: 'text', text }], source })
    if (Buffer.byteLength(JSON.stringify(message), 'utf8') > config.maxContextBytes) {
      throw new Error(`RSI-infer complete experience context exceeds maxContextBytes (${config.maxContextBytes}); increase the bound or use Standard mode.`)
    }
    return { ...decision, messages: [...decision.messages, message] }
  }, { prepend: true })

  ctx.on('agent/request', async ({ agent }, next) => {
    const resolved = await next()
    const selected = selections.get(agent)
    if (selected !== undefined) assertSameRoute(selected, resolved.provider, resolved.model)
    for (const receipt of receipts(agent)) assertSameRoute(receipt, resolved.provider, resolved.model)
    return resolved
  }, { prepend: true })
}
