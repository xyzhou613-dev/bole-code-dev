/** Office presents retained conversion results and font notices around the shared PDF view. */
import { useEffect, useState, type ReactNode } from 'react'
import type { PropsLocale, PropsRenderSlots, PropsStore, SlotHookFactory } from '@deepseek-ai/dsh-client-ui-slots'
import type { TabId } from '@deepseek-ai/dsh-client-ui-dockkit'
import { Button, FileTypeIcon, classifyFileType } from '@deepseek-ai/dsh-client-ui-primitives'
import { pathPartsOf } from '@deepseek-ai/dsh-util-workspace-path'
import type { UseSidebarRightTabInfo } from '@deepseek-ai/dsh-client-ui-sidebar-right/client'
import type { DocumentBodyOwner, DocumentPreviewProps } from '../document/contract.ts'
import { hostFileOf } from '../rpc.ts'
import { LoadingIndicator } from '../LoadingIndicator.tsx'
import type { OfficeLoadInjected } from './face.ts'
import type { OfficeEditInjected } from './face.ts'
import type { OfficeStore } from './store.ts'
import common from '../TextPreview.module.css'
import css from './OfficeBody.module.css'

declare module '@deepseek-ai/dsh-client-ui-slots' {
  interface SlotMap {
    /** PDF presentation supplied with Office-owned converted bytes. */
    'sidebar.right.tab.document.office.pdf': {
      kind: 'keyed'
      scope: 'session'
      owner: DocumentBodyOwner
      hookContext: UseSidebarRightTabInfo
      inject: { hooks: { tabInfo: SlotHookFactory<'sidebar.right.tab.document', UseSidebarRightTabInfo> } }
    }
  }
}

/** Office loading callbacks supplied by the registration's services. */
export interface OfficeBodyInjected extends OfficeLoadInjected, OfficeEditInjected {
  /** @param tab - owning tab. @param signal - tab lifetime, including hidden bodies. */
  readonly retainTab: (tab: TabId, signal: AbortSignal) => void
}

/** Office body inputs and its private PDF child. */
export type OfficeBodyProps = DocumentPreviewProps & PropsStore<OfficeStore> & OfficeBodyInjected
  & PropsLocale<'sidebarOffice'> & PropsRenderSlots<'sidebar.right.tab.document.office.pdf'>

/**
 * Load one Office revision and preserve its result while its tab remains open.
 * @param props - renderer loading request, tab state, conversion callbacks, and PDF slot.
 * @returns conversion status or the PDF scrollport.
 */
export function OfficeBody(props: OfficeBodyProps): ReactNode {
  const { tab } = props.useTabInfo()
  const { load, retainTab, loadText, saveText, resetTab, resourceAddress, t } = props
  const file = hostFileOf(resourceAddress)
  const { name } = pathPartsOf(resourceAddress)
  const isDocx = /\.docx$/iu.test(name)
  const request = props.content.kind === 'renderer' ? props.content : undefined
  const revision = request?.revision
  const held = props.useStore(state => state.byTab[tab.id])
  const view = held?.revision === revision ? held : undefined
  const settled = view?.file !== undefined || view?.failure !== undefined
  const [editing, setEditing] = useState<{ text: string; version: string } | undefined>(undefined)
  const [draft, setDraft] = useState('')
  const [saving, setSaving] = useState(false)
  const [editError, setEditError] = useState<string | undefined>(undefined)
  useEffect(() => { retainTab(tab.id, tab.signal) }, [retainTab, tab.id, tab.signal])
  // Metadata may replace callbacks without advancing the content revision.
  useEffect(() => {
    if (request === undefined || settled || tab.signal.aborted) return
    const controller = new AbortController()
    const signal = AbortSignal.any([controller.signal, tab.signal])
    load(tab.id, request.revision, hostFileOf(resourceAddress), signal, request.loaded, request.failed)
    return () => { controller.abort() }
  }, [revision, resourceAddress, tab.id, tab.signal, load, settled])
  const startEdit = (): void => {
    setEditError(undefined)
    setSaving(false)
    loadText(file, tab.signal).then((field) => {
      /* v8 ignore next -- a closing tab unmounts the body before the load resolves. */
      if (tab.signal.aborted) return
      setEditing(field)
      setDraft(field.text)
    }, (error: unknown) => {
      /* v8 ignore next -- a closing tab skips both the message and the non-Error fallback. */
      if (!tab.signal.aborted) setEditError(error instanceof Error ? error.message : t('editLoadFailed'))
    })
  }
  const cancelEdit = (): void => { setEditing(undefined); setEditError(undefined) }
  const save = (): void => {
    /* v8 ignore next -- the save button renders only while editing and is disabled while saving. */
    if (editing === undefined || saving) return
    setSaving(true)
    setEditError(undefined)
    saveText(file, draft, editing.version, tab.signal).then(() => {
      /* v8 ignore next -- aborting a tab unmounts the edit form before a write resolves. */
      if (tab.signal.aborted) return
      /* v8 ignore next -- the edit form renders only under a renderer revision. */
      resetTab(tab.id, request?.revision ?? 0)
      setEditing(undefined)
      setSaving(false)
    }, (error: unknown) => {
      /* v8 ignore next -- a closing tab unmounts the body before the write rejects. */
      if (tab.signal.aborted) return
      setEditError(error instanceof Error ? error.message : t('editSaveFailed'))
      setSaving(false)
    })
  }
  if (request === undefined) return null
  if (view === undefined) return <LoadingIndicator className={common.statusLine} label={t('loading')} />
  if (view.failure !== undefined) {
    return <div className={common.empty} data-textpreview-failed={view.failure.code}>
      <FileTypeIcon kind={classifyFileType(name)} size={36} />
      <p className={common.emptyLine}>{view.failure.message}</p>
      <Button size="sm" onClick={request.reload}>{t('retry')}</Button>
    </div>
  }
  if (view.file === undefined) return <LoadingIndicator className={common.statusLine} label={t('loading')} />
  if (editing !== undefined) {
    const unchanged = draft === editing.text
    return <div className={css.edit}>
      <div className={css.editToolbar}>
        <Button size="sm" onClick={save} disabled={saving || unchanged}>{saving ? t('saving') : t('save')}</Button>
        <Button size="sm" onClick={cancelEdit} disabled={saving}>{t('cancel')}</Button>
      </div>
      {editError !== undefined && <p className={css.editError}>{editError}</p>}
      <textarea className={css.editTextarea} value={draft} onChange={(event) => { setDraft(event.target.value) }}
        disabled={saving} spellCheck={false} />
    </div>
  }
  return <div className={css.body}>
    {isDocx && <div className={css.editToolbar}>
      <Button size="sm" onClick={startEdit}>{t('edit')}</Button>
    </div>}
    {props.renderSlot('sidebar.right.tab.document.office.pdf', {
      resourceAddress, content: { kind: 'bytes', data: view.file.data }, wrap: props.wrap, scrollportRef: props.scrollportRef,
      addResource: props.addResource, setResources: props.setResources,
    }, { entryKey: '@deepseek-ai/dsh-client-ui-sidebar-documentpreview/office', hookContext: props.useTabInfo })}
  </div>
}
