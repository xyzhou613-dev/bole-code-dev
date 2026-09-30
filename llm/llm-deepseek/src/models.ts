/** Default Bole model catalog. */
import { DEFAULT_CONTEXT_WINDOW } from './defaults.ts'
import type { DeepSeekCatalogModel } from './types.ts'

/** Advisory model entries; deployments may replace the catalog. */
export const DEFAULT_MODELS: DeepSeekCatalogModel[] = [
  {
    id: 'bole-model',
    name: 'Bole Model',
    contextWindow: DEFAULT_CONTEXT_WINDOW,
    inputModalities: ['text', 'image'],
    systemPromptUpdate: 'in-history',
  },
]
