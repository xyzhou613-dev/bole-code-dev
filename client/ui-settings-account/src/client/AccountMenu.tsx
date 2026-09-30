/** Sidebar settings launcher: a plain settings entry with no account chrome. */
import { IconSettingsOutlineMedium } from '@deepseek-ai/dsh-client-ui-primitives'
import type { InjectFace, PropsLocale, PropsRuntime } from '@deepseek-ai/dsh-client-ui-slots'
import type { AccountSectionInjected } from './AccountSection.tsx'
import css from './AccountMenu.module.css'

/** Account launcher composed by the settings shell. */
export type AccountMenuProps = PropsRuntime<'settings.launcher'> & PropsLocale<'settings.account'> & InjectFace<AccountSectionInjected>

/** A plain settings button that opens the settings shell.
 * @param props - sidebar geometry, settings navigation and localization.
 * @returns settings launcher.
 */
export function AccountMenu({ wide, openSettings, t }: AccountMenuProps) {
  return <div className={css.root}>
    <button type="button" className={css.trigger} data-collapsed={!wide} aria-label={t('settings')}
      aria-haspopup="dialog" onClick={() => { openSettings() }}>
      <IconSettingsOutlineMedium size={18} />
      {wide && <span className={css.label}>{t('settings')}</span>}
    </button>
  </div>
}
