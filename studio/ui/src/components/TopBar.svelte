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
  const sysName = $derived(LANGUAGES.find((x) => x.code === systemLang())?.name ?? '');
  /** the language in two letters (pt-BR -> PT) */
  const short = $derived(i18n.lang.split('-')[0].toUpperCase());
  const langName = $derived(LANGUAGES.find((x) => x.code === i18n.lang)?.name ?? '');
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
  <!-- the language: the globe and two letters; the native list (a select laid over them) names them in full -->
  <label class="lang" title="{t('lang.label')}: {langName}">
    <Icon name="globe" size={16} stroke={2} />
    <span class="code">{short}</span>
    <select value={i18n.choice} aria-label={t('lang.label')} onchange={(e) => i18n.set(e.currentTarget.value as Choice)}>
      <option value="system">{t('lang.system', { name: sysName })}</option>
      {#each LANGUAGES as x (x.code)}<option value={x.code}>{x.name}</option>{/each}
    </select>
  </label>
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
    white-space: nowrap;
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
  .lang {
    position: relative;
    display: flex;
    align-items: center;
    gap: 6px;
    flex: none;
    height: 30px;
    padding: 0 10px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    color: var(--label);
    cursor: pointer;
    transition: color 0.15s, border-color 0.15s;
  }
  .lang:hover, .lang:focus-within { color: var(--text); border-color: var(--wire); }
  .code { font: 600 13px/1 var(--f-value); letter-spacing: 0.04em; }
  .lang select {
    position: absolute;
    inset: 0;
    width: 100%;
    opacity: 0;
    cursor: pointer;
    font: 500 12.5px var(--f-body);
    background: var(--bg);
    color: var(--text);
  }
</style>
