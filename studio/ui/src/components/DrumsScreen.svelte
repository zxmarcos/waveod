<!--
  THESIS: your own grooves on the pedal's drum machine: twenty places, each one you can see and hear.
  FORM: the 20 places M1-M20 on the left (what plays, loop or once); the picked one's score on the right, drawn
  from what the pedal stores, with play / stop; a MIDI file comes in through a preview of what the pedal will play.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import DrumRoll from './DrumRoll.svelte';
  import DrumImport from './DrumImport.svelte';
  import { drums, pedalName } from '../lib/drums.svelte';
  import { decimal, num, t, tn } from '../lib/i18n.svelte';

  $effect(() => { untrack(() => void drums.enter()); });

  const st = $derived(drums.state);
  const i = $derived(drums.picked);
  const slot = $derived(drums.slot);
  const pat = $derived(drums.patterns[i]);
  const used = $derived(st ? st.slots.filter(Boolean).length : 0);
  const playingSlot = $derived(st?.playing && st.playing_place !== null ? st.slots[st.playing_place] : null);

  let draft = $state('');
  let input = $state<HTMLInputElement>();
  $effect(() => {
    if (drums.renaming && input) {
      draft = untrack(() => slot?.name ?? '');
      input.focus();
      input.select();
    }
  });
  function renameKey(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      e.preventDefault();
      drums.renaming = false;
      void drums.rename(i, draft);
    } else if (e.key === 'Escape') {
      e.stopPropagation();
      drums.renaming = false;
    }
  }
  function listKey(e: KeyboardEvent) {
    const d = e.key === 'ArrowDown' ? 1 : e.key === 'ArrowUp' ? -1 : 0;
    if (d) {
      e.preventDefault();
      drums.pick(Math.max(0, Math.min(19, i + d)));
      (e.currentTarget as HTMLElement).querySelector<HTMLElement>(`[data-i="${drums.picked}"]`)?.focus();
    } else if (e.key === ' ' && slot) {
      e.preventDefault();
      void drums.toggle(i);
    } else if (e.key === 'F2' && slot) {
      e.preventDefault();
      drums.renaming = true;
    }
  }
  /** the first empty place, for "Importar MIDI…" when the picked one is in use */
  const target = $derived(slot ? (st?.slots.findIndex((s) => !s) ?? -1) : i);
  const minutes = (s: number) => (s >= 60 ? `${Math.floor(s / 60)} min ${Math.round(s % 60)} s` : `${num(s, 1)} s`);
</script>

<div class="screen">
  <header class="tools">
    <div class="title">
      <h1>{t('tab.drums')}</h1>
      <span class="sub">{t('drums.sub')}</span>
    </div>
    <div class="transport" class:on={st?.playing} aria-live="polite">
      {#if st?.playing}
        <span class="eq" aria-hidden="true"><i></i><i></i><i></i></span>
        <span>{t('drums.playing')} <b>{playingSlot ? `M${(st.playing_place ?? 0) + 1} ${playingSlot.name}` : t('drums.bank_rhythm')}</b></span>
        <button onclick={() => drums.stop()}><Icon name="stop" size={14} stroke={2.4} /> {t('drums.stop')}</button>
      {:else}
        <span>{t('drums.stopped')}</span>
      {/if}
    </div>
    <button class="primary" disabled={target < 0} onclick={() => drums.importFor(target)}
            title={target < 0 ? t('drums.full_title') : t('drums.import_title', { n: target + 1 })}>
      <Icon name="file" size={16} stroke={2.1} /> {t('drums.import')}
    </button>
  </header>

  <div class="body">
    <section class="places" aria-label={t('drums.places_aria')}>
      <div class="phead"><span>{t('drums.places')}</span><span class="count">{t('drums.count', { n: used, max: st?.places ?? 20 })}</span></div>
      <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
      <ol onkeydown={listKey}>
        {#each st?.slots ?? Array(20).fill(null) as s, k (k)}
          <li>
            <button data-i={k} class:picked={k === i} class:vacant={!s} class:playing={drums.isPlaying(k)}
                    tabindex={k === i ? 0 : -1} onclick={() => drums.pick(k)}
                    ondblclick={() => (s ? drums.toggle(k) : drums.importFor(k))}
                    aria-current={k === i ? 'true' : undefined}>
              <span class="m">M{k + 1}</span>
              <span class="pn">{s ? s.name : t('drums.empty')}</span>
              {#if drums.isPlaying(k)}
                <span class="eq small" aria-label={t('drums.playing_aria')}><i></i><i></i><i></i></span>
              {:else if s}
                <span class="meta" title={s.once ? t('drums.once_title') : t('drums.loop_title')}>
                  <Icon name={s.once ? 'once' : 'loop'} size={14} stroke={2} />{s.bpm ? ` ${s.bpm}` : ''}
                </span>
              {/if}
            </button>
          </li>
        {/each}
      </ol>
    </section>

    <section class="detail" aria-label={t('drums.rhythm_aria', { n: i + 1 })}>
      {#if !st}
        <p class="note">{t('drums.loading')}</p>
      {:else if !slot}
        <div class="empty">
          <span class="big">M{i + 1}</span>
          <h2>{t('drums.vacant_title')}</h2>
          <p>{t('drums.vacant_text')}</p>
          <button class="primary" onclick={() => drums.importFor(i)}>
            <Icon name="file" size={16} stroke={2.1} /> {t('drums.import_to', { n: i + 1 })}
          </button>
        </div>
      {:else}
        <div class="head">
          <span class="label">M{i + 1}</span>
          {#if drums.renaming}
            <input class="rename" bind:this={input} value={draft} maxlength="16" spellcheck="false"
                   oninput={(e) => { draft = pedalName(e.currentTarget.value); e.currentTarget.value = draft; }}
                   aria-label={t('drums.name_label')} onkeydown={renameKey} onblur={() => (drums.renaming = false)} />
          {:else}
            <h2>{slot.name}</h2>
          {/if}
        </div>
        <dl class="facts">
          <div><dt>{t('drums.end')}</dt><dd>{slot.once ? t('drums.end_once') : t('drums.end_loop')}</dd></div>
          <div><dt>{t('drums.midi_tempo')}</dt><dd>{slot.bpm ? `${slot.bpm} BPM` : '–'}</dd></div>
          <div><dt>{t('drums.length')}</dt><dd>{pat ? `${tn('drums.beats', pat.length / 120, { n: decimal(String(pat.length / 120)) })}${slot.bpm ? ` · ${minutes((pat.length / 120) * 60 / slot.bpm)}` : ''}` : '…'}</dd></div>
          <div><dt>{t('drums.events')}</dt><dd>{t('drums.of', { n: slot.events, max: st.max_events })}</dd></div>
        </dl>
        <div class="actions">
          <button class="play" class:on={drums.isPlaying(i)} onclick={() => drums.toggle(i)} title={t('drums.space')}>
            <Icon name={drums.isPlaying(i) ? 'stop' : 'play'} size={16} stroke={2.4} />
            {drums.isPlaying(i) ? t('drums.stop') : t('drums.play')}
          </button>
          <button onclick={() => (drums.renaming = true)} title="F2"><Icon name="pencil" size={15} stroke={2} /> {t('drums.rename')}</button>
          <button onclick={() => drums.importFor(i)}><Icon name="file" size={15} stroke={2} /> {t('drums.replace')}</button>
          <button class="danger" onclick={() => drums.empty(i)}><Icon name="trash" size={15} stroke={2} /> {t('drums.empty_action')}</button>
        </div>
        {#if pat}
          <DrumRoll roll={pat} />
          <p class="hint">{t('drums.roll_hint')}</p>
        {:else}
          <p class="note">{t('drums.loading_one')}</p>
        {/if}
      {/if}
    </section>
  </div>
</div>

{#if drums.draft}<DrumImport />{/if}

<style>
  .screen { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .tools {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 16px 24px 14px;
    border-bottom: 1px solid var(--line);
  }
  .title { display: flex; align-items: baseline; gap: 12px; flex: 1; min-width: 0; }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  .sub { font-size: 13px; color: var(--label); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .transport {
    display: flex;
    align-items: center;
    gap: 10px;
    height: 36px;
    padding: 0 6px 0 14px;
    border-radius: var(--r-sm);
    border: 1px solid var(--line);
    font-size: 13.5px;
    color: var(--label);
    white-space: nowrap;
  }
  .transport:not(.on) { padding-right: 14px; }
  .transport.on { color: var(--text); border-color: #2d5a37; background: #0f1a12; }
  .transport b { font-weight: 600; }
  .transport button {
    display: flex;
    align-items: center;
    gap: 6px;
    height: 26px;
    padding: 0 9px;
    border-radius: 5px;
    font: 600 14px var(--f-display);
  }
  .transport button:hover { background: rgba(255, 255, 255, 0.07); }
  .tools > .primary, .empty .primary {
    display: flex;
    align-items: center;
    gap: 7px;
    height: 36px;
    padding: 0 14px;
    border-radius: var(--r-sm);
    background: var(--accent);
    color: var(--accent-ink);
    font: 700 15px var(--f-display);
  }
  .primary:hover:not(:disabled) { background: #f7c169; }
  .primary:disabled { background: var(--surface); color: var(--dim); cursor: default; }

  .eq { display: inline-flex; align-items: flex-end; gap: 2px; height: 14px; }
  .eq i { width: 3px; height: 100%; border-radius: 1px; background: var(--ok); animation: eq 0.9s ease-in-out infinite; }
  .eq i:nth-child(2) { animation-delay: -0.3s; }
  .eq i:nth-child(3) { animation-delay: -0.6s; }
  @keyframes eq { 0%, 100% { transform: scaleY(0.3); } 50% { transform: scaleY(1); } }
  .eq i { transform-origin: bottom; }
  @media (prefers-reduced-motion: reduce) { .eq i { animation: none; transform: scaleY(0.7); } }

  .body { flex: 1; min-height: 0; display: flex; }
  .places {
    flex: none;
    width: 320px;
    display: flex;
    flex-direction: column;
    border-right: 1px solid var(--line);
    background: var(--surface);
  }
  .phead {
    display: flex;
    justify-content: space-between;
    padding: 14px 18px 8px;
    font: 600 12.5px var(--f-display);
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--label);
  }
  .count { font: 500 12px var(--f-value); letter-spacing: 0; text-transform: none; }
  ol { list-style: none; margin: 0; padding: 0 10px 16px; overflow-y: auto; flex: 1; }
  ol button {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
    height: 38px;
    padding: 0 10px;
    border-radius: var(--r-sm);
    text-align: left;
  }
  ol button:hover { background: var(--raised); }
  ol button.picked { background: var(--raised); box-shadow: inset 0 0 0 1px var(--border-off); }
  .m { width: 30px; font: 500 13px var(--f-value); color: var(--label); flex: none; }
  .picked .m { color: var(--accent); }
  .pn { flex: 1; min-width: 0; font: 600 15.5px var(--f-display); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .vacant .pn { color: var(--dim); font-weight: 500; }
  .playing .pn { color: var(--ok); }
  .meta { display: flex; align-items: center; gap: 2px; font: 500 12px var(--f-value); color: var(--label); }

  .detail { flex: 1; min-width: 0; overflow-y: auto; padding: 22px 28px 28px; }
  .note { color: var(--label); }
  .head { display: flex; align-items: baseline; gap: 14px; margin-bottom: 14px; }
  .label { font: 500 22px var(--f-value); color: var(--accent); }
  h2 { margin: 0; font: 700 32px/1.1 var(--f-display); }
  .rename {
    flex: 1;
    max-width: 420px;
    height: 44px;
    padding: 0 10px;
    border-radius: var(--r-sm);
    background: var(--bg);
    border: 1px solid var(--accent);
    outline: none;
    font: 700 26px var(--f-display);
    user-select: text;
  }
  .facts { display: flex; gap: 36px; margin: 0 0 18px; flex-wrap: wrap; }
  .facts dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .facts dd { margin: 3px 0 0; font: 500 14.5px var(--f-value); }
  .actions { display: flex; gap: 8px; margin-bottom: 18px; flex-wrap: wrap; }
  .actions button {
    display: flex;
    align-items: center;
    gap: 7px;
    height: 36px;
    padding: 0 13px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 15px var(--f-display);
  }
  .actions button:hover { background: var(--raised); }
  .actions .play { background: var(--ok); color: #08210f; border-color: var(--ok); font-weight: 700; min-width: 150px; }
  .actions .play:hover { background: #8de8a2; }
  .actions .play.on { background: #13281a; color: var(--ok); border-color: #2d5a37; }
  .actions .danger { color: #ffb3b0; }
  .actions .danger:hover { background: #2a1312; border-color: #6b2523; }
  .hint { margin: 10px 0 0; font-size: 12.5px; color: var(--label); }
  .empty { max-width: 560px; margin: 40px auto; text-align: center; }
  .empty .big { font: 500 40px var(--f-value); color: var(--dim); }
  .empty h2 { margin: 4px 0 12px; }
  .empty p { color: var(--label); line-height: 1.55; margin: 0 0 20px; }
  .empty .primary { margin: 0 auto; }
</style>
