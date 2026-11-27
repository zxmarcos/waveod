<script lang="ts">
  import mark from '../assets/mark.png';
  import Icon from './Icon.svelte';
  import { i18n, LANGUAGES, systemLang, t, type Choice, type MsgKey } from '../lib/i18n.svelte';
  import { studio, type Screen } from '../lib/store.svelte';

  // the screens still in the classic app (python -m waveod_studio) while this one grows
  const TABS: { id: Screen; label: MsgKey; ready: boolean }[] = [
    { id: 'grid', label: 'tab.grid', ready: true },
    { id: 'presets', label: 'tab.presets', ready: true },
    { id: 'effects', label: 'tab.effects', ready: true },
    { id: 'nam', label: 'tab.nam', ready: true },
    { id: 'irs', label: 'tab.irs', ready: true },
    { id: 'drums', label: 'tab.drums', ready: true },
    { id: 'global', label: 'tab.global', ready: true },
    { id: 'perf', label: 'tab.perf', ready: true },
    { id: 'pedal', label: 'tab.pedal', ready: true },
  ];
  const l = $derived(studio.link);
  const PHASES: Record<string, MsgKey> = {
    starting: 'link.starting', searching: 'link.searching', busy: 'link.busy', silent: 'link.silent', old: 'link.old',
    failed: 'link.failed', updating: 'link.updating',
  };
  /** the transport as the backend names it ('emulador', 'demonstração': identifiers) in the page's words */
  const transportName = (tr: string) =>
    tr === 'emulador' ? t('link.emulator') : tr === 'demonstração' ? t('link.demo') : tr;
  const sysName = $derived(LANGUAGES.find((x) => x.code === systemLang())?.name ?? '');
  const where = $derived(
    !l ? t('link.initial') : l.phase !== 'connected' ? (PHASES[l.phase] ? t(PHASES[l.phase]) : l.phase) :
    l.transport === 'usb' ? `${l.name} · ${l.version}` : `${transportName(l.transport)} · ${l.version}`);
  const tone = $derived(!l || l.phase === 'starting' || l.phase === 'searching' ? 'look' :
                         l.phase === 'connected' ? 'ok' : 'bad');
</script>

<header class="top">
  <div class="brand">
    <img src={mark} alt="" width="52" height="20" />
    <span class="word">waveod <b>Studio</b></span>
  </div>
  <nav aria-label={t('top.screens')}>
    {#each TABS as tab (tab.id)}
      <button class:current={tab.id === studio.screen} disabled={!tab.ready || !studio.everConnected}
              aria-current={tab.id === studio.screen ? 'page' : undefined}
              onclick={() => tab.ready && studio.show(tab.id as Screen)}
              title={tab.ready ? '' : t('top.classic')}>{t(tab.label)}</button>
    {/each}
  </nav>
  <label class="lang" title={t('lang.label')}>
    <Icon name="globe" size={15} stroke={2} />
    <select value={i18n.choice} aria-label={t('lang.label')} onchange={(e) => i18n.set(e.currentTarget.value as Choice)}>
      <option value="system">{t('lang.system', { name: sysName })}</option>
      {#each LANGUAGES as x (x.code)}<option value={x.code}>{x.name}</option>{/each}
    </select>
  </label>
  <div class="link" class:ok={tone === 'ok'} class:bad={tone === 'bad'} class:look={tone === 'look'}>
    <span class="dot"></span>{where}
  </div>
</header>

<style>
  .top {
    display: flex;
    align-items: center;
    gap: 28px;
    height: 52px;
    flex: none;
    padding: 0 20px;
    border-bottom: 1px solid var(--line);
    background: var(--bg);
  }
  .brand { display: flex; align-items: center; gap: 10px; width: 262px; flex: none; }
  @media (max-width: 1260px) { .brand { width: auto; } nav button { padding: 0 11px; } }
  .word { font: 600 19px/1 var(--f-display); color: var(--text); letter-spacing: 0.01em; }
  .word b { font-weight: 600; color: var(--label); }
  nav { display: flex; gap: 4px; flex: 1; min-width: 0; }
  nav button {
    position: relative;
    height: 52px;
    padding: 0 14px;
    font: 600 15px/1 var(--f-display);
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--label);
  }
  nav button:disabled { color: var(--dim); cursor: default; }
  nav button.current { color: var(--text); }
  nav button.current::after {
    content: '';
    position: absolute;
    left: 14px;
    right: 14px;
    bottom: -1px;
    height: 2px;
    background: var(--accent);
  }
  .lang { display: flex; align-items: center; gap: 6px; flex: none; color: var(--label); }
  .lang select {
    height: 28px; padding: 0 6px; border-radius: var(--r-sm); background: var(--bg); color: var(--label);
    border: 1px solid var(--border-off); font: 500 12.5px var(--f-body); cursor: pointer; outline: none;
  }
  .lang select:hover, .lang select:focus { color: var(--text); border-color: var(--wire); }
  .link {
    display: flex;
    align-items: center;
    gap: 8px;
    font: 500 12.5px var(--f-value);
    color: var(--label);
    white-space: nowrap;
    flex: none;
  }
  .dot { flex: none; width: 8px; height: 8px; border-radius: 50%; background: var(--dim); }
  .ok .dot { background: var(--ok); }
  .bad .dot { background: var(--error); }
  .look .dot { background: var(--accent); animation: blink 1.2s ease-in-out infinite; }
  @keyframes blink { 50% { opacity: 0.25; } }
  @media (prefers-reduced-motion: reduce) { .look .dot { animation: none; } }
</style>
