<script lang="ts">
  // The connection along the bottom: where the link stands, and once connected the device, its firmware, the way it
  // is reached and the editor protocol it speaks.
  import { t, type MsgKey } from '../lib/i18n.svelte';
  import { studio } from '../lib/store.svelte';

  const l = $derived(studio.link);
  const PHASES: Record<string, MsgKey> = {
    starting: 'link.starting', searching: 'link.searching', busy: 'link.busy', silent: 'link.silent', old: 'link.old',
    failed: 'link.failed', updating: 'link.updating',
  };
  /** the transport as the backend names it ('emulador', 'demonstração': identifiers) in the page's words */
  const transportName = (tr: string) =>
    tr === 'usb' ? 'USB' : tr === 'emulador' ? t('link.emulator') : tr === 'demonstração' ? t('link.demo') : tr;
  const connected = $derived(l?.phase === 'connected');
  const state = $derived(
    !l ? t('link.initial') : !connected ? (PHASES[l.phase] ? t(PHASES[l.phase]) : l.phase) :
    l.transport === 'usb' ? l.name : transportName(l.transport));
  const tone = $derived(!l || l.phase === 'starting' || l.phase === 'searching' ? 'look' : connected ? 'ok' : 'bad');
</script>

<footer class="status" aria-label={t('status.aria')}>
  <span class="item main" class:ok={tone === 'ok'} class:bad={tone === 'bad'} class:look={tone === 'look'}>
    <span class="dot"></span>{state}
  </span>
  {#if connected && l}
    <span class="item">{t('status.firmware', { version: l.version })}</span>
    {#if l.transport === 'usb'}<span class="item">USB</span>{/if}
    <span class="item">{t('status.protocol', { n: l.protocol })}</span>
  {/if}
</footer>

<style>
  .status {
    display: flex;
    align-items: center;
    height: 26px;
    flex: none;
    padding: 0 20px;
    border-top: 1px solid var(--line);
    background: var(--bg);
    font: 500 12px/1 var(--f-value);
    color: var(--label);
    white-space: nowrap;
    overflow: hidden;
  }
  .item { display: flex; align-items: center; gap: 8px; padding: 0 14px; border-left: 1px solid var(--line); }
  .item:first-child { padding-left: 0; border-left: 0; }
  .main { color: var(--text); }
  .dot { flex: none; width: 8px; height: 8px; border-radius: 50%; background: var(--dim); }
  .ok .dot { background: var(--ok); }
  .bad .dot { background: var(--error); }
  .look .dot { background: var(--accent); animation: blink 1.2s ease-in-out infinite; }
  @keyframes blink { 50% { opacity: 0.25; } }
  @media (prefers-reduced-motion: reduce) { .look .dot { animation: none; } }
</style>
