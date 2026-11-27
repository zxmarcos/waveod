<!--
  THESIS: the pedal's CONTROLES / PARA ONDE screens at desk size: every key on the left, what the picked one does
  on the right, and where the picked link goes below it. Nothing reaches the pedal before "Gravar no pedal".
  FORM: key rail (footswitches with their LED colour and card name, pedals, modulators, MIDI CCs) | the key's
  links as cards, the global ones greyed under them | the picked link's destination, what, when / curve, values.
-->
<script lang="ts">
  import Icon from './Icon.svelte';
  import ModWave from './ModWave.svelte';
  import ModKnobs from './ModKnobs.svelte';
  import * as C from '../lib/controls';
  import { ctl } from '../lib/ctl.svelte';
  import { decimal, t } from '../lib/i18n.svelte';
  import { presetLabel, studio } from '../lib/store.svelte';
  import type { CtlLink, CtlParam } from '../lib/types';

  const o = $derived(ctl.open!);
  const key = $derived(ctl.key);
  const l = $derived(ctl.link);
  const cur = $derived(l ? C.current(l, ctl.blocks) : ['', '']);
  const items = $derived(cur[0] ? ctl.choicesOf(cur[0]) : []);
  const when = $derived(l ? C.whenChoice(l) : null);
  const prm = $derived(l && l.action === C.PARAM ? C.linkParam(l, ctl.blocks) : undefined);
  const [lo, hi] = $derived(C.ends(key));
  const used = $derived(ctl.table.links.filter((x) => !C.unset(x)).length);
  const isFs = $derived(key < C.FOOTSWITCH_KEYS);
  const where = $derived(C.everyMode(key) ? t('ctl.all_modes') : t('ctl.in_mode', { mode: C.modeName(ctl.mode).toLowerCase() }));
  const alsoModes = (x: CtlLink) =>
    C.everyMode(x.key) ? [] : C.MODES.filter((m) => x.modes & m.mode && m.mode !== ctl.mode).map((m) => m.name);
  const fsName = (k: number) => (k === C.AGAIN ? t('ctl.again') : C.keyName(k));

  let ccText = $state('');
  function addCc() {
    if (ctl.addCc(Number(ccText.trim()))) ccText = '';
    else studio.say(t('ctl.cc_range'), 'error');
  }

  /** a number typed in: applied on Enter / leaving the field, the field then shows what was kept */
  function typed(e: Event, apply: (v: number) => void) {
    const el = e.currentTarget as HTMLInputElement;
    const v = Number(el.value.trim().replace(',', '.'));
    if (el.value.trim() !== '' && Number.isFinite(v)) apply(v);
    el.value = el.defaultValue;
  }
  const clamp = (v: number, a: number, b: number) => Math.max(a, Math.min(b, v));
  const comma = decimal;
  const setEnd = (which: 'off' | 'on', p: CtlParam) => (v: number) => { if (l) l[which] = C.quantize(p, v); };

  /** a modulator: the parameter its drawing names on the right (the picked link, else its first), at 0 and 100 % */
  const modEnds = $derived.by(() => {
    if (!C.isMod(key)) return null;
    const pick = l && l.action === C.PARAM && C.linkParam(l, ctl.blocks) ? l : null;
    const x = pick ?? ctl.own.map((i) => ctl.table.links[i]).find((y) => y.action === C.PARAM && C.linkParam(y, ctl.blocks));
    const p = x ? C.linkParam(x, ctl.blocks) : undefined;
    if (!x || !p) return null;
    return { name: C.brief(x, ctl.blocks), lo: comma(C.paramFmt(p, x.off)), hi: comma(C.paramFmt(p, x.on)) };
  });
  let focusStep = $state<number | null>(null);

  function keydown(e: KeyboardEvent) {
    if (e.key === 'Escape' && !studio.dialog && (e.target as HTMLElement).tagName !== 'SELECT') {
      e.preventDefault();
      if (ctl.sel !== null) ctl.sel = null;
      else void ctl.close();
    } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
      e.preventDefault();
      e.stopPropagation();
      if (ctl.dirty) void ctl.write();
    }
  }
</script>

<svelte:window onkeydown={keydown} />

<div class="ctl">
  <header class="tools">
    <button class="back" onclick={() => ctl.close()} title={t(o.from === 'grid' ? 'ctl.back_grid' : 'ctl.back_presets')}>
      <Icon name="prev" size={18} stroke={2.2} />
    </button>
    <div class="title">
      {#if o.kind === 'global'}
        <h1>{t('ctl.title_global')}</h1>
        <span class="sub">{t('ctl.global_sub')}</span>
      {:else}
        <h1>{t('ctl.title')} <span class="lbl">{presetLabel(o.index)}</span></h1>
        <span class="sub">{o.name || t('ctl.unnamed')}{o.own ? '' : t('ctl.no_table')}</span>
      {/if}
    </div>
    <div class="seg" role="radiogroup" aria-label={t('ctl.fs_mode')}>
      {#each C.MODES as m (m.mode)}
        <button role="radio" aria-checked={ctl.mode === m.mode} class:on={ctl.mode === m.mode}
                onclick={() => ctl.setMode(m.mode)}>{m.label}</button>
      {/each}
    </div>
    <span class="usage" class:full={used >= C.CONTROL_MAX}>{t('ctl.usage', { used, max: C.CONTROL_MAX })}</span>
    <button class="ghost" onclick={() => ctl.close()} disabled={ctl.busy}>{t('common.cancel')}</button>
    <button class="primary" onclick={() => ctl.write()} disabled={!ctl.dirty || ctl.busy || ctl.loading} title="Ctrl+S">
      <Icon name="save" size={16} stroke={2.1} /> {ctl.busy ? t('common.writing') : t('ctl.write')}
    </button>
  </header>

  {#if ctl.loading}
    <p class="loading">{t('ctl.loading')}</p>
  {:else}
    <div class="cols">
      <nav class="keys" aria-label={t('ctl.keys')}>
        {#each ctl.keyGroups as g (g.title)}
          <h3>{g.title}</h3>
          {#each g.keys as k (k)}
            {@const s = ctl.summary(k)}
            <button class="key" class:on={key === k} onclick={() => ctl.setKey(k)} aria-current={key === k}>
              {#if k < C.FOOTSWITCH_KEYS}
                <i class="led" class:auto={!ctl.table.colour[k]} style:--led={C.COLOUR_CSS[ctl.table.colour[k] & 7]}></i>
              {:else if C.isMod(k) && ctl.mods}
                {#if ctl.mods[k - C.MOD1].type === 'off'}
                  <i class="led mod"></i>
                {:else}
                  <span class="mthumb"><ModWave m={ctl.mods[k - C.MOD1]} mini /></span>
                {/if}
              {/if}
              <span class="kn">{fsName(k)}</span>
              {#if k < C.FOOTSWITCH_KEYS && ctl.table.label[k]}<span class="card">{ctl.table.label[k]}</span>{/if}
              <span class="what" class:g={s.global} title={s.global ? t('ctl.from_global') : ''}>
                {s.silenced ? t('ctl.nothing_short') : s.text}{#if s.global}<em>G</em>{/if}{#if s.more}<b>+{s.more}</b>{/if}
              </span>
            </button>
          {/each}
        {/each}
        <form class="addcc" onsubmit={(e) => { e.preventDefault(); addCc(); }}>
          <input bind:value={ccText} placeholder="CC 1-127" inputmode="numeric" aria-label={t('ctl.cc_number')} />
          <button type="submit"><Icon name="plus" size={14} stroke={2.4} /> CC</button>
        </form>
      </nav>

      <section class="main" aria-label={t('ctl.links_of', { key: C.keyName(key) })}>
        <div class="khead">
          <h2>{fsName(key)}</h2>
          <span class="where">{where}</span>
          {#if isFs}
            <label class="field card-name">
              <span>{t('ctl.card_name')}</span>
              <input value={ctl.table.label[key]} maxlength={C.LABEL_LEN} placeholder={t('ctl.automatic')} spellcheck="false"
                     oninput={(e) => ctl.setLabel(e.currentTarget.value)} />
            </label>
            <label class="field">
              <span>{t('ctl.led_colour')}</span>
              <span class="swatch-sel">
                <i class="led" class:auto={!ctl.table.colour[key]} style:--led={C.COLOUR_CSS[ctl.table.colour[key] & 7]}></i>
                <select value={ctl.table.colour[key] & 7} onchange={(e) => ctl.setColour(Number(e.currentTarget.value))}>
                  {#each C.colourNames() as c, n (n)}<option value={n}>{c}</option>{/each}
                </select>
              </span>
            </label>
          {/if}
        </div>

        {#if ctl.mod}
          {@const m = ctl.mod}
          <div class="modbox">
            <div class="mhead">
              <div class="seg types" role="radiogroup" aria-label={t('ctl.mod_type')}>
                {#each C.MOD_TYPES as ty (ty.id)}
                  <button role="radio" aria-checked={m.type === ty.id} class:on={m.type === ty.id}
                          onclick={() => ctl.setModType(ty.id)}>{ty.label}</button>
                {/each}
              </div>
              {#if m.type === 'lfo'}
                <div class="shapes" role="radiogroup" aria-label={t('ctl.lfo_shape')}>
                  {#each C.MOD_SHAPES as s (s.id)}
                    <button role="radio" aria-checked={m.shape === s.id} class:on={m.shape === s.id} title={s.label}
                            aria-label={s.label} onclick={() => (m.shape = s.id)}>
                      <ModWave {m} mini shape={s.id} />
                    </button>
                  {/each}
                  <span class="shape-name">{C.MOD_SHAPES.find((s) => s.id === m.shape)?.label ?? ''}</span>
                </div>
              {/if}
            </div>
            <div class="mbody">
              <div class="mwave"><ModWave {m} bpm={ctl.bpm} ends={modEnds} selStep={focusStep} onstep={(i) => (focusStep = i)} /></div>
              <ModKnobs {m} bpm={ctl.bpm} />
            </div>
            {#if m.type !== 'off' && !ctl.own.length}
              <p class="hint">{t('ctl.mod_hint')}</p>
            {/if}
          </div>
        {/if}

        <ul class="links">
          {#each ctl.own as i (i)}
            {@const x = ctl.table.links[i]}
            {@const w = C.whenText(x)}
            {@const also = alsoModes(x)}
            <li class:sel={ctl.sel === i}>
              <button class="lk" onclick={() => ctl.select(i)}>
                {#if w}<span class="pill" class:hot={x.when !== C.PRESS}>{w}</span>{/if}
                <span class="lt" class:unset={C.unset(x)}>{C.linkText(x, ctl.blocks, ctl.scenes)}</span>
                {#if also.length && w}<span class="also" title={t('ctl.also_title')}>{t('ctl.also', { modes: also.join(', ') })}</span>{/if}
              </button>
              <button class="rm" onclick={() => ctl.remove(i)} title={t(also.length ? 'ctl.remove_mode' : 'ctl.remove')} aria-label={t('common.remove')}>
                <Icon name="close" size={15} stroke={2.2} />
              </button>
            </li>
          {/each}
          {#each ctl.inherited as gi (`g${gi}`)}
            {@const x = ctl.glob!.links[gi]}
            {@const w = C.whenText(x)}
            <li class="glob">
              <span class="lk">
                <span class="pill g"><Icon name="globe" size={12} stroke={2} /> GLOBAL</span>
                {#if w}<span class="pill">{w}</span>{/if}
                <span class="lt">{C.linkText(x, ctl.blocks, ctl.scenes)}</span>
              </span>
              <button class="mini" onclick={() => ctl.copyGroup(gi)} title={t('ctl.copy_title')}>{t('ctl.copy')}</button>
              <button class="mini" onclick={() => ctl.nothing(gi)} title={t('ctl.no_globals_title')}>{t('ctl.no_globals')}</button>
            </li>
          {/each}
          {#if !ctl.own.length && !ctl.inherited.length}
            <li class="none">{t(C.isMod(key) && ctl.mod?.type === 'off' ? 'ctl.mod_is_off' : 'ctl.no_links')}</li>
          {/if}
        </ul>
        <button class="add" onclick={() => ctl.add()} disabled={used >= C.CONTROL_MAX || (C.isMod(key) && !ctl.blocks.length)}>
          <Icon name="plus" size={15} stroke={2.3} /> {t('ctl.new_link')}
        </button>

        {#if l}
          <div class="editor" aria-label={t('ctl.picked')}>
            <div class="row">
              <label class="field">
                <span>{t('ctl.dest')}</span>
                <select value={cur[0]} onchange={(e) => ctl.pickDest(e.currentTarget.value)}>
                  {#if !cur[0]}<option value="" disabled>{t('ctl.choose')}</option>{/if}
                  {#each ctl.dests as d (d.id)}<option value={d.id}>{d.label}</option>{/each}
                </select>
              </label>
              {#if cur[0]}
                <label class="field">
                  <span>{t('ctl.what')}</span>
                  <select value={cur[1]} onchange={(e) => ctl.pickChoice(cur[0], e.currentTarget.value)}>
                    {#if !cur[1]}<option value="" disabled>?</option>{/if}
                    {#each items as c (c.id)}<option value={c.id}>{c.label}</option>{/each}
                  </select>
                </label>
              {/if}
              {#if when}
                <label class="field">
                  <span>{t(when[0] === 'curve' ? 'ctl.curve' : 'ctl.when')}</span>
                  <select value={when[2]} onchange={(e) => ctl.setWhen(Number(e.currentTarget.value))}>
                    {#each when[1] as w, n (n)}<option value={n}>{w}</option>{/each}
                  </select>
                </label>
              {/if}
            </div>
            {#if l.action === C.PARAM && prm}
              {@const hint = t('common.range', { min: comma(C.paramFmt(prm, prm.min)), max: comma(C.paramFmt(prm, prm.max)) })}
              <div class="row">
                {@render number(lo, comma(C.paramFmt(prm, l.off)), hint, setEnd('off', prm))}
                <span class="arrow">»</span>
                {@render number(hi, comma(C.paramFmt(prm, l.on)), hint, setEnd('on', prm))}
              </div>
            {:else if l.action === C.MIDI}
              <div class="row">
                {@render number(C.isPc(l) ? 'PC' : 'CC', `${l.arg}`, t('common.range', { min: 0, max: 127 }), (v) => { if (l) l.arg = Math.round(clamp(v, 0, 127)); })}
                <label class="field">
                  <span>{t('ctl.channel')}</span>
                  <select value={l.slot & 15} onchange={(e) => { if (l) l.slot = Number(e.currentTarget.value); }}>
                    {#each Array.from({ length: 16 }, (_, n) => n) as n (n)}<option value={n}>{n + 1}</option>{/each}
                  </select>
                </label>
                {#if !C.isPc(l)}
                  {@render number(lo, `${Math.round(l.off)}`, t('common.range', { min: 0, max: 127 }), (v) => { if (l) l.off = Math.round(clamp(v, 0, 127)); })}
                  <span class="arrow">»</span>
                  {@render number(hi, `${Math.round(l.on)}`, t('common.range', { min: 0, max: 127 }), (v) => { if (l) l.on = Math.round(clamp(v, 0, 127)); })}
                {/if}
              </div>
            {:else if C.unset(l)}
              <p class="hint">{t('ctl.dest_hint')}</p>
            {/if}
          </div>
        {:else if ctl.own.length}
          <p class="hint pick">{t('ctl.pick_hint')}</p>
        {/if}
      </section>
    </div>
  {/if}
</div>

{#snippet number(title: string, value: string, hint: string, apply: (v: number) => void)}
  <label class="field">
    <span>{title}</span>
    {#key value}
      <input class="num" {value} defaultValue={value} onchange={(e) => typed(e, apply)}
             onkeydown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); }} />
    {/key}
    <small>{hint}</small>
  </label>
{/snippet}

<style>
  .ctl { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .tools {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 24px 12px 14px;
    border-bottom: 1px solid var(--line);
  }
  .back { display: grid; place-items: center; width: 36px; height: 36px; border-radius: var(--r-sm); color: var(--label); }
  .back:hover { background: var(--raised); color: var(--text); }
  .title { display: flex; flex-direction: column; min-width: 0; margin-right: auto; }
  h1 { margin: 0; font: 700 26px/1.05 var(--f-display); white-space: nowrap; }
  h1 .lbl { font: 500 19px var(--f-value); color: var(--accent); margin-left: 6px; }
  .sub { font-size: 12.5px; color: var(--label); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--bg); border: 1px solid var(--line); }
  .seg button { height: 30px; padding: 0 12px; border-radius: 7px; font: 600 14.5px var(--f-display); color: var(--label); white-space: nowrap; }
  .seg button:hover:not(.on) { color: var(--text); background: var(--track); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  .usage { font: 500 12.5px var(--f-value); color: var(--label); white-space: nowrap; }
  .usage.full { color: var(--error); }
  .tools > .ghost, .tools > .primary {
    display: flex; align-items: center; gap: 7px; height: 36px; padding: 0 14px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); font: 600 15px var(--f-display); white-space: nowrap;
  }
  .tools > .ghost:hover:not(:disabled) { background: var(--raised); }
  .tools > .primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); font-weight: 700; }
  .tools > .primary:hover:not(:disabled) { background: #f7c169; }
  .tools > button:disabled { opacity: 0.45; cursor: default; }
  .loading { margin: 40px auto; color: var(--label); }

  .cols { flex: 1; min-height: 0; display: flex; }
  .keys {
    flex: none; width: 268px; overflow-y: auto; padding: 8px 10px 16px; background: var(--surface);
    border-right: 1px solid var(--line);
  }
  .keys h3 {
    margin: 14px 8px 6px; font: 600 11.5px var(--f-display); letter-spacing: 0.1em; text-transform: uppercase; color: var(--label);
  }
  .key {
    display: flex; align-items: center; gap: 9px; width: 100%; height: 34px; padding: 0 10px; border-radius: var(--r-sm);
    text-align: left; color: var(--text);
  }
  .key:hover:not(.on) { background: var(--raised); }
  .key.on { background: var(--raised); box-shadow: inset 3px 0 0 var(--accent); }
  .kn { font: 600 15px var(--f-display); white-space: nowrap; }
  .card { font: 500 11.5px var(--f-value); color: var(--label); padding: 1px 5px; border: 1px solid var(--border-off); border-radius: 4px; }
  .what {
    flex: 1; min-width: 0; text-align: right; font: 500 12.5px var(--f-body); color: var(--label);
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .what.g { color: var(--off); }
  .what em {
    margin-left: 6px; padding: 0 4px; border-radius: 3px; border: 1px solid var(--border-off);
    font: 500 10.5px/14px var(--f-value); font-style: normal; color: var(--off);
  }
  .what b { margin-left: 5px; font: 500 11.5px var(--f-value); color: var(--accent); }
  .key.on .what:not(.g) { color: var(--text); }
  .led { flex: none; width: 10px; height: 10px; border-radius: 50%; background: var(--led); box-shadow: 0 0 6px var(--led); }
  .led.auto { background: conic-gradient(#ff4d4a, #ffd53d, #39d36b, #3de0ff, #3d8bff, #ff4dd2, #ff4d4a); box-shadow: none; opacity: 0.75; }
  .led.mod { width: 22px; height: 2px; background: var(--dim); box-shadow: none; border-radius: 1px; }
  .addcc { display: flex; gap: 6px; margin: 10px 4px 0; }
  .addcc input {
    width: 0; flex: 1; height: 32px; padding: 0 10px; border-radius: var(--r-sm); background: var(--bg);
    border: 1px solid var(--border-off); outline: none; font: 500 13px var(--f-value); user-select: text;
  }
  .addcc input:focus { border-color: var(--accent); }
  .addcc button {
    display: flex; align-items: center; gap: 4px; height: 32px; padding: 0 12px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); font: 600 14px var(--f-display);
  }
  .addcc button:hover { background: var(--raised); }

  .main { flex: 1; min-width: 0; overflow-y: auto; padding: 18px 28px 28px; }
  .khead { display: flex; align-items: flex-end; gap: 14px; flex-wrap: wrap; }
  h2 { margin: 0; font: 700 30px/1 var(--f-display); }
  .where { margin-right: auto; padding-bottom: 3px; font-size: 13px; color: var(--label); }
  .field { display: flex; flex-direction: column; gap: 5px; }
  .field > span:first-child {
    font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label);
  }
  .field small { font: 500 11px var(--f-value); color: var(--off); }
  select, .field input {
    height: 34px; padding: 0 10px; border-radius: var(--r-sm); background: var(--bg); color: var(--text);
    border: 1px solid var(--border-off); outline: none; font: 500 14px var(--f-body);
  }
  select { min-width: 150px; padding-right: 6px; cursor: pointer; }
  select:focus, .field input:focus { border-color: var(--accent); }
  .field input { user-select: text; }
  .field input.num { width: 112px; font: 500 14px var(--f-value); }
  .card-name input { width: 128px; font: 500 14px var(--f-value); text-transform: uppercase; }
  .swatch-sel { display: flex; align-items: center; gap: 8px; }

  .modbox {
    display: flex; flex-direction: column; gap: 14px; margin-top: 16px; padding: 14px 16px 16px;
    border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line);
  }
  .mhead { display: flex; align-items: center; flex-wrap: wrap; gap: 12px 18px; }
  .mbody { display: flex; flex-direction: column; align-items: flex-start; gap: 10px; }
  .mwave { width: 100%; max-width: 640px; min-width: 0; }
  .types { align-self: flex-start; }
  .shapes { display: flex; align-items: center; gap: 4px; }
  .shapes button {
    display: grid; place-items: center; width: 52px; height: 34px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); color: var(--label); background: var(--bg);
  }
  .shapes button:hover:not(.on) { color: var(--text); border-color: var(--wire); }
  .shapes button.on { color: var(--accent); border-color: var(--accent); background: #1d1810; }
  .shape-name { margin-left: 8px; font: 600 14px var(--f-display); color: var(--text); white-space: nowrap; }
  .mthumb { display: grid; place-items: center; flex: none; width: 22px; color: var(--accent); }
  .mthumb :global(svg) { width: 22px; height: 10px; }

  .links { list-style: none; margin: 18px 0 0; padding: 0; display: flex; flex-direction: column; gap: 6px; }
  .links li {
    display: flex; align-items: center; gap: 6px; min-height: 42px; padding-right: 6px; border-radius: 8px;
    background: var(--surface); border: 1px solid var(--line);
  }
  .links li.sel { border-color: var(--accent); background: #1d1810; }
  .lk { flex: 1; min-width: 0; display: flex; align-items: center; gap: 10px; align-self: stretch; padding: 0 12px; text-align: left; }
  .lt { font: 500 15px var(--f-body); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .lt.unset { color: var(--off); font-style: italic; }
  .also { margin-left: auto; font: 500 11.5px var(--f-value); color: var(--off); white-space: nowrap; }
  .pill {
    flex: none; display: inline-flex; align-items: center; gap: 4px; padding: 3px 7px; border-radius: 5px;
    font: 600 11.5px/1 var(--f-display); letter-spacing: 0.06em; color: var(--label); border: 1px solid var(--border-off);
  }
  .pill.hot { color: var(--accent); border-color: #5a4114; }
  .pill.g { color: var(--off); }
  .rm { display: grid; place-items: center; width: 30px; height: 30px; border-radius: var(--r-sm); color: var(--off); }
  .rm:hover { background: var(--raised); color: var(--error); }
  .links li.glob { background: transparent; border-style: dashed; }
  .glob .lt { color: var(--label); }
  .mini { height: 28px; padding: 0 10px; border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 600 13.5px var(--f-display); white-space: nowrap; }
  .mini:hover { background: var(--raised); }
  .none { padding: 0 14px; color: var(--off); border-style: dashed !important; background: transparent !important; }
  .add {
    display: inline-flex; align-items: center; gap: 6px; height: 34px; margin-top: 10px; padding: 0 14px;
    border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 600 14.5px var(--f-display);
  }
  .add:hover:not(:disabled) { background: var(--raised); }
  .add:disabled { opacity: 0.45; cursor: default; }

  .editor {
    margin-top: 18px; padding: 16px 18px; border-radius: var(--r-md); background: var(--raised);
    border: 1px solid var(--border-off); display: flex; flex-direction: column; gap: 16px;
  }
  .row { display: flex; align-items: flex-start; flex-wrap: wrap; gap: 14px; }
  .arrow { align-self: center; margin-top: 6px; font: 500 18px var(--f-value); color: var(--off); }
  .hint { margin: 0; font-size: 13px; color: var(--label); }
  .hint.pick { margin-top: 16px; }
  .modbox .hint { flex-basis: 100%; }
</style>
