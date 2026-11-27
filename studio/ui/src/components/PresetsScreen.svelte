<!--
  THESIS: the pedal's memory as the pedal has it: 60 banks of four footswitches, every preset's chain at a glance.
  FORM: bank rows (01 … 60) of four cards A-D; the chain under each name as a strip of its blocks' colours;
  the picked preset's details on the right. Drag a card to move it (Ctrl copies); arrows walk, Enter activates.
-->
<script lang="ts">
  import Icon from './Icon.svelte';
  import PresetDetailPanel from './PresetDetail.svelte';
  import { menuInfo } from '../lib/menus';
  import { ctl } from '../lib/ctl.svelte';
  import { isVacant, presetLabel, studio } from '../lib/store.svelte';
  import { t } from '../lib/i18n.svelte';
  import type { PresetEntry } from '../lib/types';

  let query = $state('');
  let over = $state(-1);
  let dragFrom = $state(-1);
  let copying = $state(false);
  let list = $state<HTMLDivElement>();

  const banks = $derived.by(() => {
    const out: PresetEntry[][] = [];
    for (let b = 0; b < 60; b++) out.push(studio.presets.slice(b * 4, b * 4 + 4));
    return out;
  });
  const q = $derived(query.trim().toLowerCase());
  const matches = (p: PresetEntry) =>
    !q || (!isVacant(p) && p.name.toLowerCase().includes(q)) || presetLabel(p.index).toLowerCase() === q;
  const shownBanks = $derived(banks.map((row, b) => ({ b, row })).filter(({ row }) => !q || row.some(matches)));
  const counts = $derived({
    stored: studio.presets.filter((p) => !isVacant(p)).length,
    vacant: studio.presets.filter((p) => isVacant(p)).length,
  });

  // ---------------------------------------------------------------- clicks, keys
  function choose(i: number) {
    const pl = studio.placing;
    if (pl) {
      studio.placing = null;
      if (pl.op === 'move') void studio.movePreset(pl.src, i);
      else void studio.copyPreset(pl.src, i);
      return;
    }
    studio.renaming = false;
    studio.pick(i);
  }
  let clip = -1;                                          // Ctrl+C: the preset Ctrl+V copies
  function key(e: KeyboardEvent) {
    if ((e.target as HTMLElement).tagName === 'INPUT') return;
    const i = studio.picked ?? 0;
    const step = { ArrowRight: 1, ArrowLeft: -1, ArrowDown: 4, ArrowUp: -4 }[e.key];
    if (step) {
      e.preventDefault();
      const to = Math.max(0, Math.min(239, i + step));
      studio.pick(to);
      list?.querySelector<HTMLElement>(`[data-i="${to}"]`)?.focus();
    } else if (e.key === 'Enter' && studio.picked !== null && !studio.placing) {
      e.preventDefault();
      void studio.activate(i);
    } else if (e.key === 'F2' && studio.picked !== null) {
      e.preventDefault();
      studio.renaming = true;
    } else if (e.key === 'Escape' && studio.placing) {
      e.stopPropagation();
      studio.placing = null;
    } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'c' && !isVacant(studio.presets[i])) {
      clip = i;
      studio.say(t('presets.clip', { label: presetLabel(i) }));
    } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'v' && clip >= 0 && studio.picked !== null) {
      void studio.copyPreset(clip, studio.picked);
    }
  }

  // ---------------------------------------------------------------- drag and drop
  const TYPE = 'application/x-waveod-preset';
  function dragStart(e: DragEvent, i: number) {
    e.dataTransfer?.setData(TYPE, String(i));
    if (e.dataTransfer) e.dataTransfer.effectAllowed = 'copyMove';
    dragFrom = i;
  }
  function dragOver(e: DragEvent, i: number) {
    if (!e.dataTransfer?.types.includes(TYPE)) return;
    e.preventDefault();
    copying = e.ctrlKey || e.altKey;
    e.dataTransfer.dropEffect = copying ? 'copy' : 'move';
    over = i;
  }
  function drop(e: DragEvent, i: number) {
    e.preventDefault();
    const src = Number(e.dataTransfer?.getData(TYPE));
    over = dragFrom = -1;
    if (Number.isNaN(src) || src === i) return;
    if (e.ctrlKey || e.altKey) void studio.copyPreset(src, i);
    else void studio.movePreset(src, i);
  }
  function dragEnd() {
    over = dragFrom = -1;
  }
</script>

<div class="screen">
  <section class="browse" aria-label={t('presets.aria')}>
    <header class="tools">
      <div class="title">
        <h1>{t('tab.presets')}</h1>
        <span class="count">{t('presets.count', { stored: counts.stored, vacant: counts.vacant })}</span>
      </div>
      <label class="search">
        <Icon name="search" size={17} stroke={2} />
        <input type="search" placeholder={t('presets.search')} bind:value={query} />
      </label>
      <button class="ghost" onclick={() => ctl.openGlobal()} title={t('presets.global_title')}>
        <Icon name="globe" size={16} stroke={2} /> {t('presets.global')}
      </button>
      <button class="ghost" onclick={() => studio.restore()} title={t('presets.restore_title')}>
        {t('presets.restore')}
      </button>
      <button class="primary" onclick={() => studio.backup()} title={t('presets.backup_title')}>
        <Icon name="save" size={16} stroke={2.1} /> {t('store.backup')}
      </button>
    </header>

    {#if studio.placing}
      {@const pl = studio.placing}
      <div class="placing" role="status">
        <span><b>{t(pl.op === 'move' ? 'presets.place_move' : 'presets.place_copy', { label: presetLabel(pl.src) })}</b>
          “{studio.presets[pl.src]?.name || t('ctl.unnamed')}”:
          {t(pl.op === 'move' ? 'presets.place_hint_move' : 'presets.place_hint')}</span>
        <button onclick={() => (studio.placing = null)}>{t('common.cancel')} <kbd>Esc</kbd></button>
      </div>
    {:else if dragFrom >= 0}
      <div class="placing hint" role="status">
        <span>{t('presets.drop')} <b>{t(copying ? 'presets.drop_copy' : 'presets.drop_move')}</b> {presetLabel(dragFrom)}{copying ? '' : t('presets.drop_slide')}
          · {t('presets.hold')} <kbd>Ctrl</kbd> {t('presets.hold_copy')}</span>
      </div>
    {/if}

    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <div class="banks" bind:this={list} onkeydown={key} class:picking={!!studio.placing}>
      {#each shownBanks as { b, row } (b)}
        <div class="bank">
          <span class="num" aria-hidden="true">{String(b + 1).padStart(2, '0')}</span>
          {#each row as p (p.index)}
            {@const d = studio.details[p.index]}
            {@const vacant = isVacant(p)}
            <button class="card" data-i={p.index} class:vacant class:active={p.active} class:picked={studio.picked === p.index}
                    class:dim={!matches(p)} class:over={over === p.index} class:from={dragFrom === p.index}
                    aria-label="{presetLabel(p.index)} {vacant ? t('presets.vacant') : p.name || t('ctl.unnamed')}{p.active ? t('presets.active_suffix') : ''}"
                    aria-current={studio.picked === p.index ? 'true' : undefined}
                    tabindex={studio.picked === p.index || (studio.picked === null && p.index === 0) ? 0 : -1}
                    draggable={!vacant} ondragstart={(e) => dragStart(e, p.index)} ondragend={dragEnd}
                    ondragover={(e) => dragOver(e, p.index)} ondragleave={() => (over = -1)} ondrop={(e) => drop(e, p.index)}
                    onclick={() => choose(p.index)} ondblclick={() => !studio.placing && studio.activate(p.index)}>
              <span class="top">
                <span class="fs">{'ABCD'[p.index % 4]}</span>
                <span class="name">{vacant ? t('presets.vacant') : p.name || t('ctl.unnamed')}</span>
                {#if p.active}
                  <span class="live" title={studio.rig?.dirty ? t('presets.live_dirty') : t('presets.live')}
                        class:dirty={studio.rig?.dirty}></span>
                {/if}
              </span>
              <span class="strip" aria-hidden="true">
                {#if d}
                  {#each d.blocks as blk (blk.slot)}
                    <i class:off={!blk.on} style:--c={menuInfo(blk.menu).color}></i>
                  {/each}
                {:else if !vacant}
                  <i class="wait"></i><i class="wait"></i><i class="wait"></i>
                {/if}
              </span>
            </button>
          {/each}
        </div>
      {:else}
        <p class="none">{t('presets.none', { query })}</p>
      {/each}
    </div>
  </section>

  <PresetDetailPanel />
</div>

<style>
  .screen { flex: 1; display: flex; min-height: 0; min-width: 0; }
  .browse { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .tools {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 16px 24px 14px;
    border-bottom: 1px solid var(--line);
  }
  .title { display: flex; align-items: baseline; gap: 12px; flex: 1; min-width: 0; }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  .count { font: 500 12.5px var(--f-value); color: var(--label); white-space: nowrap; }
  @media (max-width: 1260px) { .count { display: none; } }
  .search {
    display: flex;
    align-items: center;
    gap: 8px;
    flex: 0 1 280px;
    min-width: 150px;
    height: 36px;
    padding: 0 10px;
    border-radius: var(--r-sm);
    background: var(--surface);
    border: 1px solid var(--line);
    color: var(--label);
  }
  .search:focus-within { border-color: var(--dim); }
  .search input { flex: 1; min-width: 0; background: none; border: 0; outline: none; height: 100%; }
  .tools > button {
    flex: none;
    display: flex;
    align-items: center;
    gap: 7px;
    height: 36px;
    padding: 0 14px;
    border-radius: var(--r-sm);
    font: 600 15px var(--f-display);
  }
  .ghost { border: 1px solid var(--border-off); color: var(--text); }
  .ghost:hover { background: var(--raised); }
  .primary { background: var(--accent); color: var(--accent-ink); font-weight: 700 !important; }
  .primary:hover { background: #f7c169; }

  .placing {
    display: flex;
    align-items: center;
    gap: 14px;
    margin: 12px 24px 0;
    padding: 9px 10px 9px 14px;
    border-radius: var(--r-md);
    background: #2a1f0c;
    border: 1px solid #5a4114;
    font-size: 13.5px;
    color: #f6dcae;
  }
  .placing span { flex: 1; }
  .placing.hint { background: var(--raised); border-color: var(--border-off); color: var(--text); }
  .placing button { height: 30px; padding: 0 10px; border-radius: var(--r-sm); font: 600 14px var(--f-display); }
  .placing button:hover { background: rgba(255, 255, 255, 0.06); }
  kbd {
    font: 500 11px var(--f-value);
    padding: 1px 5px;
    border-radius: 4px;
    border: 1px solid currentColor;
    opacity: 0.75;
  }

  .banks { flex: 1; overflow-y: auto; padding: 12px 24px 28px; }
  .bank {
    display: grid;
    grid-template-columns: 40px repeat(4, minmax(0, 1fr));
    gap: 8px;
    margin-bottom: 8px;
  }
  .num {
    align-self: center;
    font: 500 15px var(--f-value);
    color: var(--dim);
    text-align: right;
    padding-right: 6px;
  }
  .card {
    position: relative;
    display: flex;
    flex-direction: column;
    justify-content: center;
    gap: 7px;
    height: 62px;
    padding: 0 12px 0 10px;
    border-radius: var(--r-md);
    background: var(--surface);
    border: 1px solid var(--line);
    text-align: left;
    transition: border-color 0.12s, background 0.12s;
  }
  .card:hover { border-color: var(--border-off); background: var(--raised); }
  .card.picked { border-color: var(--label); background: var(--raised); }
  .card.active::before {
    content: '';
    position: absolute;
    left: -1px;
    top: 10px;
    bottom: 10px;
    width: 3px;
    border-radius: 0 3px 3px 0;
    background: var(--accent);
  }
  .card.vacant { background: transparent; border-style: dashed; border-color: var(--line); }
  .card.vacant:hover { border-color: var(--border-off); }
  .card.dim { opacity: 0.28; }
  .card.from { opacity: 0.45; }
  .card.over, .picking .card:hover { border-color: var(--accent); border-style: solid; background: #1e1a12; }
  .top { display: flex; align-items: baseline; gap: 8px; min-width: 0; }
  .fs { font: 500 12px var(--f-value); color: var(--label); flex: none; }
  .active .fs { color: var(--accent); }
  .name {
    flex: 1;
    min-width: 0;
    font: 600 16px/1.15 var(--f-display);
    color: var(--text);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .vacant .name { color: var(--dim); font-weight: 500; }
  .live { width: 8px; height: 8px; border-radius: 50%; background: var(--ok); flex: none; align-self: center; }
  .live.dirty { background: var(--accent); }
  .strip { display: flex; gap: 3px; height: 6px; padding-left: 20px; overflow: hidden; }
  .strip i { flex: 0 1 14px; min-width: 4px; height: 6px; border-radius: 2px; background: var(--c); }
  .strip i.off { background: none; box-shadow: inset 0 0 0 1px var(--dim); }
  .strip i.wait { background: var(--track); }
  .none { color: var(--label); margin: 24px 0; text-align: center; }
</style>
