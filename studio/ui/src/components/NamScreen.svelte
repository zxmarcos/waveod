<!--
  The Clone slots (20; 10 on a firmware before waveod's own 10): what each holds (empty, the original's capture, a NAM capture and the rate it runs at), and a
  .nam going into one after saying exactly what the slot loses. A slot is the player's own capture: never a click away
  from being overwritten.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import { KIND, nam } from '../lib/nam.svelte';
  import { decimal, num, t } from '../lib/i18n.svelte';

  $effect(() => { untrack(() => void nam.enter()); });

  const slots = $derived(nam.slots);
  const s = $derived(nam.slot);
  const d = $derived(nam.draft);
  const n = $derived(slots ? slots.filter((x) => x.kind === KIND.NAM || x.kind === KIND.NAM48).length : 0);
  const total = $derived(slots?.length ?? 20);
  const tone = (k: number) => (k === KIND.EMPTY ? 'empty' : k === KIND.ORIGINAL ? 'orig' : k === KIND.BROKEN ? 'bad' : 'nam');
  const khz = (hz?: number) => (hz ? `${decimal(String(Math.round(hz / 100) / 10))} kHz` : '–');
  const db = (v?: number | null) => (v === undefined || v === null ? '–' : `${num(v, 1)} dB`);
  function take(node: HTMLInputElement) { node.focus(); node.select(); }
  function key(e: KeyboardEvent) {
    if (e.key === 'Escape' && d && !d.busy) {
      e.stopPropagation();
      nam.draft = null;
    }
  }
</script>

<div class="screen">
  <header class="tools">
    <div class="title">
      <h1>NAM</h1>
      <span class="sub">{t('nam.sub', { total })}</span>
    </div>
    {#if slots}<span class="count">{t('nam.count', { n, total })}</span>{/if}
  </header>

  {#if !slots}
    <p class="note">{t('nam.loading')}</p>
  {:else}
    <div class="body">
      <div class="slots" role="listbox" aria-label={t('nam.slots_aria')}>
        {#each slots as x (x.slot)}
          <button role="option" aria-selected={x.slot === nam.picked} class="slot {tone(x.kind)}"
                  class:picked={x.slot === nam.picked} onclick={() => (nam.picked = x.slot)}
                  ondblclick={() => { nam.picked = x.slot; void nam.choose(); }}>
            <span class="num">{x.slot + 1}</span>
            <span class="nm">{x.kind === KIND.EMPTY ? t('nam.empty') : x.name}</span>
            <span class="kd">{x.kind === KIND.EMPTY ? '' : x.kind_text}</span>
          </button>
        {/each}
      </div>

      {#if s}
        <section class="detail {tone(s.kind)}" aria-label={t('nam.slot', { n: s.slot + 1 })}>
          <span class="label">{t('nam.slot', { n: s.slot + 1 })}</span>
          <h2>{s.kind === KIND.EMPTY ? t('nam.empty_title') : s.name}</h2>
          <p class="kind"><Icon name="nam" size={17} stroke={2} /> {s.kind_text}</p>
          {#if s.sample_rate !== undefined}
            <dl class="facts">
              <div><dt>{t('nam.trained')}</dt><dd>{khz(s.sample_rate)}</dd></div>
              <div><dt>{t('nam.runs')}</dt><dd>{s.kind === KIND.NAM48 ? t('nam.runs_48') : t('nam.runs_44')}</dd></div>
              <div><dt>{t('nam.loudness')}</dt><dd>{db(s.loudness)}</dd></div>
              <div><dt>{t('nam.crc')}</dt><dd>{(s.crc ?? 0).toString(16).toUpperCase().padStart(8, '0')}</dd></div>
            </dl>
          {/if}
          <p class="hint">
            {#if s.kind === KIND.EMPTY}{t('nam.hint_empty')}
            {:else if s.kind === KIND.ORIGINAL}{t('nam.hint_original')}
            {:else if s.kind === KIND.BROKEN}{t('nam.hint_broken')}
            {:else}{t('nam.hint_nam')}{/if}
          </p>
          <button class={s.kind === KIND.EMPTY ? 'primary' : 'warn'} onclick={() => nam.choose()}>
            <Icon name="file" size={16} stroke={2.1} /> {s.kind === KIND.EMPTY ? t('nam.write_capture') : t('nam.replace_capture')}
          </button>
        </section>
      {/if}
    </div>
  {/if}
</div>

{#if d}
  {@const old = slots?.[d.slot]}
  <div class="veil" role="presentation" onclick={() => !d.busy && (nam.draft = null)}></div>
  <div class="sheet" role="dialog" aria-modal="true" aria-labelledby="nam-title" tabindex="-1" onkeydown={key}>
    <h3 id="nam-title">{t('nam.confirm_title', { n: d.slot + 1 })}</h3>
    <p class="file"><Icon name="file" size={15} stroke={2} /> {d.file.file}</p>
    <dl class="facts">
      <div><dt>{t('nam.network')}</dt><dd>{d.engine === 'nam' ? t('nam.weights', { n: d.file.weights }) : d.file.arch}</dd></div>
      <div><dt>{t('nam.trained')}</dt><dd>{khz(d.file.sample_rate)}</dd></div>
      <div><dt>{t('nam.loudness')}</dt><dd>{d.file.loudness === null ? t('nam.loudness_none') : db(d.file.loudness)}</dd></div>
    </dl>
    <label class="name"><span>{t('nam.name_label')}</span>
      <input bind:value={d.name} maxlength="16" spellcheck="false" use:take />
    </label>
    {#if d.file.lite && d.file.convert}
      <div class="seg" role="radiogroup" aria-label={t('nam.engine_aria')}>
        <button role="radio" aria-checked={d.engine === 'nam'} class:on={d.engine === 'nam'} disabled={d.busy}
                onclick={() => (d.engine = 'nam')}>{t('nam.engine_nam')}</button>
        <button role="radio" aria-checked={d.engine === 'clone'} class:on={d.engine === 'clone'} disabled={d.busy}
                onclick={() => (d.engine = 'clone')}>{t('nam.engine_clone')}</button>
      </div>
    {/if}
    {#if d.engine === 'clone'}
      <p class="hint">{t('nam.convert_hint', { arch: d.file.arch, di: d.file.di === 'editor' ? t('nam.di_editor') : t('nam.di_waveod') })}</p>
    {/if}
    {#if d.engine === 'nam' && d.file.rate48}
      <div class="seg" role="radiogroup" aria-label={t('nam.rate_aria')}>
        <button role="radio" aria-checked={!d.native} class:on={!d.native} onclick={() => (d.native = false)}>{t('nam.rate_48')}</button>
        <button role="radio" aria-checked={d.native} class:on={d.native} onclick={() => (d.native = true)}>{t('nam.rate_44')}</button>
      </div>
      <p class="hint">{d.native ? t('nam.native_hint') : t('nam.resampled_hint')}</p>
    {/if}
    <p class="lost" class:safe={!old || old.kind === KIND.EMPTY}>
      {#if !old || old.kind === KIND.EMPTY}{t('nam.slot_empty')}
      {:else}{t('nam.replaces_before', { n: d.slot + 1 })} <b>{old.name}</b> {t('nam.replaces_after', { kind: old.kind_text })}{/if}
    </p>
    <footer>
      <button onclick={() => (nam.draft = null)} disabled={d.busy}>{t('common.cancel')}</button>
      <button class={!old || old.kind === KIND.EMPTY ? 'primary' : 'danger'} disabled={d.busy || !d.name.trim()}
              onclick={() => nam.write()}>{!old || old.kind === KIND.EMPTY ? t('nam.write_to', { n: d.slot + 1 }) : t('nam.replace_in', { n: d.slot + 1 })}</button>
    </footer>
  </div>
{/if}

<style>
  .screen { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .tools { display: flex; align-items: center; gap: 18px; padding: 16px 24px 14px; border-bottom: 1px solid var(--line); }
  .title { display: flex; align-items: baseline; gap: 12px; flex: 1; min-width: 0; }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  .sub { font-size: 13px; color: var(--label); white-space: nowrap; }
  .count { font: 500 12.5px var(--f-value); color: var(--label); }
  .note { margin: 24px; color: var(--label); }
  .body { flex: 1; min-height: 0; display: flex; gap: 0; }
  .slots {
    flex: none; width: 520px; display: grid; grid-template-columns: repeat(2, 1fr); gap: 8px; align-content: start;
    padding: 18px 20px; overflow-y: auto; border-right: 1px solid var(--line); background: var(--surface);
  }
  .slot {
    display: grid; grid-template-columns: 30px 1fr; grid-template-rows: auto auto; column-gap: 8px; align-items: center;
    height: 62px; padding: 0 12px; border-radius: var(--r-md); border: 1px solid var(--line); background: var(--bg); text-align: left;
    --c: #FD9976;
  }
  .slot:hover { border-color: var(--border-off); }
  .slot.picked { border-color: var(--label); background: var(--raised); }
  .slot.empty { border-style: dashed; background: transparent; }
  .slot.orig { --c: var(--label); }
  .slot.bad { --c: var(--error); }
  .num { grid-row: 1 / 3; font: 500 18px var(--f-value); color: var(--c); }
  .empty .num { color: var(--dim); }
  .nm { font: 600 15.5px var(--f-display); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .empty .nm { color: var(--dim); font-weight: 500; }
  .kd { font: 600 11px var(--f-display); letter-spacing: 0.04em; text-transform: uppercase; color: var(--c); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .detail { flex: 1; min-width: 0; overflow-y: auto; padding: 24px 28px; --c: #FD9976; }
  .detail.orig { --c: var(--label); }
  .detail.bad { --c: var(--error); }
  .detail.empty { --c: var(--dim); }
  .label { font: 500 14px var(--f-value); color: var(--accent); }
  h2 { margin: 4px 0 6px; font: 700 32px/1.1 var(--f-display); }
  .kind { display: flex; align-items: center; gap: 7px; margin: 0 0 16px; font: 600 14px var(--f-display); color: var(--c); }
  .facts { display: flex; gap: 32px; flex-wrap: wrap; margin: 0 0 14px; }
  .facts dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .facts dd { margin: 3px 0 0; font: 500 14px var(--f-value); }
  .hint { margin: 0 0 16px; max-width: 620px; font-size: 13.5px; line-height: 1.55; color: var(--label); }
  .detail button, .sheet footer button {
    display: flex; align-items: center; gap: 7px; height: 36px; padding: 0 14px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); font: 600 15px var(--f-display);
  }
  .primary { background: var(--accent) !important; color: var(--accent-ink); border-color: var(--accent) !important; font-weight: 700 !important; }
  .primary:hover:not(:disabled) { background: #f7c169 !important; }
  .warn { color: #f6c15a; }
  .warn:hover { background: #2a1f0c; border-color: #5a4114 !important; }

  .veil { position: fixed; inset: 0; z-index: 60; background: rgba(5, 5, 4, 0.62); }
  .sheet {
    position: fixed; z-index: 61; left: 50%; top: 45%; transform: translate(-50%, -50%); width: min(560px, calc(100vw - 48px));
    padding: 20px 22px 18px; border-radius: 14px; background: var(--raised); border: 1px solid var(--border-off); box-shadow: var(--float-shadow);
  }
  .sheet:focus { outline: none; }
  .sheet h3 { margin: 0 0 6px; font: 700 22px var(--f-display); }
  .file { display: flex; align-items: center; gap: 6px; margin: 0 0 14px; font: 500 13px var(--f-value); color: var(--label); }
  .name { display: flex; flex-direction: column; gap: 6px; margin-bottom: 14px; }
  .name span { font: 600 12px var(--f-display); letter-spacing: 0.06em; text-transform: uppercase; color: var(--label); }
  .name input { height: 36px; padding: 0 10px; border-radius: var(--r-sm); background: var(--bg); border: 1px solid var(--border-off); outline: none; font: 600 17px var(--f-display); user-select: text; }
  .name input:focus { border-color: var(--accent); }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); margin-bottom: 6px; }
  .seg button { flex: 1; height: 30px; border-radius: 7px; font: 600 14px var(--f-display); color: var(--label); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  .seg button:disabled { opacity: 0.6; cursor: default; }
  .lost { margin: 10px 0 16px; font-size: 14px; line-height: 1.5; color: #f6dcae; }
  .lost.safe { color: var(--label); }
  .lost b { font-weight: 600; color: var(--text); }
  .sheet footer { display: flex; justify-content: flex-end; gap: 8px; }
  .sheet footer button:hover:not(:disabled) { background: var(--track); }
  .sheet footer button:disabled { opacity: 0.5; cursor: default; }
  .danger { background: #3a1716 !important; color: #ffd6d4; border-color: #6b2523 !important; }
  .danger:hover:not(:disabled) { background: #4a1c1b !important; }
  @media (max-width: 1240px) { .slots { width: 380px; grid-template-columns: 1fr; } }
</style>
