<!--
  Waiting for the pedal: what is missing in words a player acts on (switch it on, plug the cable, close the other
  program, update the firmware), and the Studio keeps looking by itself; nobody has to close and open the app.
  Full window before the first connection; over the dimmed screens when the pedal goes away later.
-->
<script lang="ts">
  import Icon from './Icon.svelte';
  import { studio } from '../lib/store.svelte';
  import { pedal } from '../lib/pedal.svelte';
  import { t, type MsgKey } from '../lib/i18n.svelte';

  let { overlay = false }: { overlay?: boolean } = $props();

  const s = $derived(studio.link);
  const phase = $derived(s?.phase ?? 'starting');
  const loading = $derived(phase === 'connected');           // found; its data on the way
  const emu = $derived(s?.transport === 'emulador');
  const tone = $derived(phase === 'old' || phase === 'failed' ? 'bad' : phase === 'busy' || phase === 'silent' ? 'warn' : 'look');

  const TITLES: Record<string, MsgKey> = {
    starting: 'connect.starting',
    searching: 'connect.searching',
    busy: 'connect.busy',
    silent: 'connect.silent',
    old: 'connect.old',
    failed: 'connect.failed',
    connected: 'connect.connected',
    updating: 'connect.updating',
  };
  const title = $derived(t(overlay && phase === 'searching' ? 'connect.lost' : TITLES[phase] ?? 'connect.searching'));
  let retrying = $state(false);
  async function retry() {
    retrying = true;
    await studio.retryLink();
    retrying = false;
  }
</script>

<div class="wait" class:overlay role="status" aria-live="polite">
  <div class="card">
    <div class="beacon {tone}" class:still={tone === 'bad'} aria-hidden="true">
      <span class="ring"></span><span class="ring two"></span>
      <span class="core"><Icon name="pedal" size={34} stroke={1.8} /></span>
    </div>
    <h1>{title}</h1>

    {#if loading}
      <p class="lead">{t('connect.found', { name: s?.name || t('connect.pedal'), version: s?.version ?? '' })}</p>
    {:else if phase === 'starting' && emu}
      <p class="lead">{t('connect.emu_starting')}</p>
    {:else if phase === 'searching' || phase === 'starting'}
      <p class="lead">
        {overlay ? t('connect.lost_lead') : t('connect.wait_lead')}
      </p>
      <ol class="steps">
        <li><b>{t('connect.power_b')}</b> {t('connect.power')}</li>
        <li><b>{t('connect.usb_b')}</b> {t('connect.usb')}</li>
        <li><b>{t('connect.close_b')}</b> {t('connect.close')}</li>
      </ol>
    {:else if phase === 'busy'}
      <p class="lead">{t('connect.busy_lead', { name: s?.name ?? '' })}</p>
      <ol class="steps">
        <li><b>{t('connect.busy_close_b')}</b> {t('connect.busy_close')}</li>
        <li>{t('connect.busy_free')}</li>
      </ol>
    {:else if phase === 'silent'}
      <p class="lead">{t('connect.silent_lead', { name: s?.name ?? '' })}</p>
      <ol class="steps">
        <li>{t('connect.silent_boot_a')} <b>{t('connect.silent_boot_b')}</b> {t('connect.silent_boot_c')}</li>
        <li>{t('connect.silent_fw_a')} <b>{t('connect.silent_fw_b')}</b> {t('connect.silent_fw_c')} <b>{t('connect.silent_fw_d')}</b>{t('connect.silent_fw_e')}</li>
      </ol>
      <button class="fw" onclick={() => pedal.pickFirmware()}><Icon name="file" size={15} stroke={2} /> {t('connect.write_fw')}</button>
    {:else if phase === 'old'}
      <p class="lead">{t('connect.old_lead', { version: s?.version ?? '', protocol: s?.protocol ?? '' })}</p>
      <ol class="steps">
        <li>{t('connect.old_step1')}</li>
        <li>{t('connect.old_step2')}</li>
      </ol>
      <button class="fw" onclick={() => pedal.pickFirmware()}><Icon name="file" size={15} stroke={2} /> {t('connect.write_fw')}</button>
    {:else if phase === 'updating'}
      <p class="lead">{t('connect.updating_lead')}</p>
    {:else if phase === 'failed'}
      <p class="lead">{s?.message}</p>
    {/if}

    {#if !loading && phase !== 'failed' && phase !== 'updating'}
      <div class="foot">
        <span class="mono">
          {s?.message && phase === 'searching' && s.code !== 'py.link_none' ? s.message + ' · ' : ''}{t('connect.attempt', { n: s?.attempts ?? 0 })}
        </span>
        <button onclick={retry} disabled={retrying || (phase === 'starting' && emu)}>{t('connect.retry')}</button>
      </div>
    {/if}
  </div>
</div>

<style>
  .wait {
    flex: 1;
    display: grid;
    place-items: center;
    padding: 24px;
    min-height: 0;
    overflow-y: auto;
  }
  .wait.overlay {
    position: fixed;
    inset: 52px 0 0;
    z-index: 55;
    background: rgba(10, 10, 9, 0.78);
    backdrop-filter: blur(3px);
    animation: fade 0.25s;
  }
  .card { width: min(560px, 100%); text-align: center; }
  .overlay .card {
    padding: 28px 32px 22px;
    border-radius: 16px;
    background: var(--raised);
    border: 1px solid var(--border-off);
    box-shadow: var(--float-shadow);
  }
  .beacon { position: relative; width: 112px; height: 112px; margin: 0 auto 22px; --c: var(--accent); }
  .beacon.warn { --c: #f6c15a; }
  .beacon.bad { --c: var(--error); }
  .core {
    position: absolute;
    inset: 22px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    border: 2px solid var(--c);
    color: var(--c);
    background: var(--bg);
  }
  .ring {
    position: absolute;
    inset: 22px;
    border-radius: 50%;
    border: 2px solid var(--c);
    opacity: 0;
    animation: ping 2.4s var(--ease-out) infinite;
  }
  .ring.two { animation-delay: 1.2s; }
  .still .ring { display: none; }
  @keyframes ping { 0% { transform: scale(1); opacity: 0.55; } 100% { transform: scale(1.9); opacity: 0; } }
  @media (prefers-reduced-motion: reduce) { .ring { animation: none; } }
  h1 { margin: 0 0 10px; font: 700 30px/1.15 var(--f-display); }
  .lead { margin: 0 auto 18px; max-width: 480px; color: var(--label); font-size: 14.5px; line-height: 1.55; }
  .steps {
    list-style: none;
    counter-reset: s;
    margin: 0 auto 22px;
    padding: 0;
    max-width: 440px;
    text-align: left;
  }
  .steps li {
    counter-increment: s;
    position: relative;
    padding: 9px 0 9px 40px;
    border-top: 1px solid var(--line);
    font-size: 14px;
    line-height: 1.45;
  }
  .steps li:last-child { border-bottom: 1px solid var(--line); }
  .steps li::before {
    content: counter(s);
    position: absolute;
    left: 6px;
    top: 9px;
    width: 22px;
    height: 22px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    background: var(--surface);
    border: 1px solid var(--border-off);
    font: 500 12px var(--f-value);
    color: var(--label);
  }
  .steps b { font-weight: 500; color: var(--text); }
  .fw {
    display: inline-flex; align-items: center; gap: 7px; height: 36px; padding: 0 14px; margin: 0 auto 20px;
    border-radius: var(--r-sm); background: var(--accent); color: var(--accent-ink); font: 700 15px var(--f-display);
  }
  .fw:hover { background: #f7c169; }
  .foot { display: flex; align-items: center; justify-content: center; gap: 14px; }
  .mono { font: 500 12px var(--f-value); color: var(--dim); }
  .foot button {
    height: 34px;
    padding: 0 14px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 14.5px var(--f-display);
  }
  .foot button:hover:not(:disabled) { background: var(--track); }
  .foot button:disabled { opacity: 0.5; cursor: default; }
  @keyframes fade { from { opacity: 0; } }
</style>
