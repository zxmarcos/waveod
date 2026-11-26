<!--
  THESIS: the pedal's CONTROLES / PARA ONDE screens at desk size: every key on the left, what the picked one does
  on the right, and where the picked link goes below it. Nothing reaches the pedal before "Gravar no pedal".
  FORM: key rail (footswitches with their LED colour and card name, pedals, modulators, MIDI CCs) | the key's
  links as cards, the global ones greyed under them | the picked link's destination, what, when / curve, values.
-->
<script lang="ts">
  import Icon from './Icon.svelte';
  import * as C from '../lib/controls';
  import { ctl } from '../lib/ctl.svelte';
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
  const where = $derived(C.everyMode(key) ? 'todos os modos' : `modo ${C.MODE_NAMES[ctl.mode].toLowerCase()}`);
  const alsoModes = (x: CtlLink) =>
    C.everyMode(x.key) ? [] : C.MODES.filter((m) => x.modes & m.mode && m.mode !== ctl.mode).map((m) => m.name);
  const fsName = (k: number) => (k === C.AGAIN ? 'PRESET ATIVO DE NOVO' : C.keyName(k));

  let ccText = $state('');
  function addCc() {
    if (ctl.addCc(Number(ccText.trim()))) ccText = '';
    else studio.say('CC de 1 a 127 (menos 68 e 69, o tempo)', 'error');
  }

  /** a number typed in: applied on Enter / leaving the field, the field then shows what was kept */
  function num(e: Event, apply: (v: number) => void) {
    const el = e.currentTarget as HTMLInputElement;
    const v = Number(el.value.trim().replace(',', '.'));
    if (el.value.trim() !== '' && Number.isFinite(v)) apply(v);
    el.value = el.defaultValue;
  }
  const clamp = (v: number, a: number, b: number) => Math.max(a, Math.min(b, v));
  const comma = (s: string) => s.replace('.', ',');
  const setEnd = (which: 'off' | 'on', p: CtlParam) => (v: number) => { if (l) l[which] = C.quantize(p, v); };

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
    <button class="back" onclick={() => ctl.close()} title={o.from === 'grid' ? 'Voltar à grade (Esc)' : 'Voltar aos presets (Esc)'}>
      <Icon name="prev" size={18} stroke={2.2} />
    </button>
    <div class="title">
      {#if o.kind === 'global'}
        <h1>Controles globais</h1>
        <span class="sub">Valem em todo preset que não tem ligação própria na tecla</span>
      {:else}
        <h1>Controles <span class="lbl">{presetLabel(o.index)}</span></h1>
        <span class="sub">{o.name || 'sem nome'}{o.own ? '' : ' · sem tabela própria: as ligações vêm dos campos de footswitch do formato original'}</span>
      {/if}
    </div>
    <div class="seg" role="radiogroup" aria-label="Modo dos footswitches">
      {#each C.MODES as m (m.mode)}
        <button role="radio" aria-checked={ctl.mode === m.mode} class:on={ctl.mode === m.mode}
                onclick={() => ctl.setMode(m.mode)}>{m.label}</button>
      {/each}
    </div>
    <span class="usage" class:full={used >= C.CONTROL_MAX}>{used} de {C.CONTROL_MAX} ligações</span>
    <button class="ghost" onclick={() => ctl.close()} disabled={ctl.busy}>Cancelar</button>
    <button class="primary" onclick={() => ctl.write()} disabled={!ctl.dirty || ctl.busy || ctl.loading} title="Ctrl+S">
      <Icon name="save" size={16} stroke={2.1} /> {ctl.busy ? 'Gravando…' : 'Gravar no pedal'}
    </button>
  </header>

  {#if ctl.loading}
    <p class="loading">Lendo os controles do pedal…</p>
  {:else}
    <div class="cols">
      <nav class="keys" aria-label="Teclas">
        {#each ctl.keyGroups as g (g.title)}
          <h3>{g.title}</h3>
          {#each g.keys as k (k)}
            {@const s = ctl.summary(k)}
            <button class="key" class:on={key === k} onclick={() => ctl.setKey(k)} aria-current={key === k}>
              {#if k < C.FOOTSWITCH_KEYS}
                <i class="led" class:auto={!ctl.table.colour[k]} style:--led={C.COLOUR_CSS[ctl.table.colour[k] & 7]}></i>
              {:else if C.isMod(k) && ctl.mods}
                <i class="led mod" class:live={ctl.mods[k - C.MOD1].type !== 'off'}></i>
              {/if}
              <span class="kn">{fsName(k)}</span>
              {#if k < C.FOOTSWITCH_KEYS && ctl.table.label[k]}<span class="card">{ctl.table.label[k]}</span>{/if}
              <span class="what" class:g={s.global} title={s.global ? 'dos controles globais' : ''}>
                {s.silenced ? 'nada' : s.text}{#if s.global}<em>G</em>{/if}{#if s.more}<b>+{s.more}</b>{/if}
              </span>
            </button>
          {/each}
        {/each}
        <form class="addcc" onsubmit={(e) => { e.preventDefault(); addCc(); }}>
          <input bind:value={ccText} placeholder="CC 1-127" inputmode="numeric" aria-label="Número do CC MIDI" />
          <button type="submit"><Icon name="plus" size={14} stroke={2.4} /> CC</button>
        </form>
      </nav>

      <section class="main" aria-label="Ligações de {C.keyName(key)}">
        <div class="khead">
          <h2>{fsName(key)}</h2>
          <span class="where">{where}</span>
          {#if isFs}
            <label class="field card-name">
              <span>Nome no card</span>
              <input value={ctl.table.label[key]} maxlength={C.LABEL_LEN} placeholder="automático" spellcheck="false"
                     oninput={(e) => ctl.setLabel(e.currentTarget.value)} />
            </label>
            <label class="field">
              <span>Cor do LED</span>
              <span class="swatch-sel">
                <i class="led" class:auto={!ctl.table.colour[key]} style:--led={C.COLOUR_CSS[ctl.table.colour[key] & 7]}></i>
                <select value={ctl.table.colour[key] & 7} onchange={(e) => ctl.setColour(Number(e.currentTarget.value))}>
                  {#each C.COLOUR_NAMES as c, n (n)}<option value={n}>{c}</option>{/each}
                </select>
              </span>
            </label>
          {/if}
        </div>

        {#if ctl.mod}
          {@const m = ctl.mod}
          <div class="modbox">
            <label class="field">
              <span>Tipo</span>
              <select value={m.type} onchange={(e) => ctl.setModType(e.currentTarget.value as typeof m.type)}>
                {#each C.MOD_TYPES as t (t.id)}<option value={t.id}>{t.label}</option>{/each}
              </select>
            </label>
            {#if m.type === 'lfo'}
              <label class="field">
                <span>Forma</span>
                <select value={m.shape} onchange={(e) => (m.shape = e.currentTarget.value)}>
                  {#each C.MOD_SHAPES as s (s.id)}<option value={s.id}>{s.label}</option>{/each}
                </select>
              </label>
            {/if}
            {#if m.type === 'lfo' || m.type === 'seq'}
              <label class="field">
                <span>Sincronia</span>
                <select value={m.sync % C.DIVISIONS.length} onchange={(e) => (m.sync = Number(e.currentTarget.value))}>
                  {#each C.DIVISIONS as d, n (n)}<option value={n}>{d}</option>{/each}
                </select>
              </label>
              {#if !m.sync}
                {@render number('Velocidade', comma(m.rate.toFixed(2)), '0,05 a 20 Hz', (v) => (m.rate = Math.round(clamp(v, 0.05, 20) * 100) / 100))}
              {/if}
            {/if}
            {#if m.type === 'lfo'}
              {@render number('Fase inicial', `${m.phase}`, '0 a 95 %', (v) => (m.phase = Math.round(clamp(v, 0, 95))))}
            {:else if m.type === 'env'}
              {@render number('Sensibilidade', `${m.sens}`, '-20 a 40 dB', (v) => (m.sens = Math.round(clamp(v, -20, 40))))}
              {@render number('Ataque', `${m.attack}`, '1 a 2000 ms', (v) => (m.attack = Math.round(clamp(v, 1, 2000))))}
              {@render number('Retorno', `${m.release}`, '10 a 5000 ms', (v) => (m.release = Math.round(clamp(v, 10, 5000))))}
            {:else if m.type === 'seq'}
              <label class="field">
                <span>Passos</span>
                <select value={m.steps} onchange={(e) => (m.steps = Number(e.currentTarget.value))}>
                  {#each Array.from({ length: C.MOD_STEPS }, (_, n) => n + 1) as n (n)}<option value={n}>{n}</option>{/each}
                </select>
              </label>
              {@render number('Deslize', `${m.glide}`, '0 a 100 %', (v) => (m.glide = Math.round(clamp(v, 0, 100))))}
            {/if}
            {#if m.type === 'seq'}
              <div class="steps" aria-label="Valores dos passos (0 a 100)">
                {#each m.step.slice(0, m.steps) as v, n (n)}
                  <label class="step">
                    <span class="bar"><i style:height="{v}%"></i></span>
                    <input value={v} defaultValue={String(v)} aria-label="Passo {n + 1}"
                           onchange={(e) => num(e, (x) => (m.step[n] = Math.round(clamp(x, 0, 100))))} />
                    <small>{n + 1}</small>
                  </label>
                {/each}
              </div>
            {/if}
            {#if m.type !== 'off' && !ctl.own.length}
              <p class="hint">Ligue o modulador a um parâmetro: Nova ligação, abaixo.</p>
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
                {#if also.length && w}<span class="also" title="A mesma ligação vale nestes modos; ao editá-la aqui ela vira só deste modo">também {also.join(', ')}</span>{/if}
              </button>
              <button class="rm" onclick={() => ctl.remove(i)} title="Remover a ligação{also.length ? ' deste modo' : ''}" aria-label="Remover">
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
              <button class="mini" onclick={() => ctl.copyGroup(gi)} title="Copia as ligações globais desta tecla para o preset, para editar">Copiar</button>
              <button class="mini" onclick={() => ctl.nothing(gi)} title="A tecla não faz nada aqui neste preset">Sem globais</button>
            </li>
          {/each}
          {#if !ctl.own.length && !ctl.inherited.length}
            <li class="none">{C.isMod(key) && ctl.mod?.type === 'off' ? 'Modulador desligado.' : 'Nenhuma ligação.'}</li>
          {/if}
        </ul>
        <button class="add" onclick={() => ctl.add()} disabled={used >= C.CONTROL_MAX || (C.isMod(key) && !ctl.blocks.length)}>
          <Icon name="plus" size={15} stroke={2.3} /> Nova ligação
        </button>

        {#if l}
          <div class="editor" aria-label="A ligação escolhida">
            <div class="row">
              <label class="field">
                <span>Para onde</span>
                <select value={cur[0]} onchange={(e) => ctl.pickDest(e.currentTarget.value)}>
                  {#if !cur[0]}<option value="" disabled>escolha…</option>{/if}
                  {#each ctl.dests as d (d.id)}<option value={d.id}>{d.label}</option>{/each}
                </select>
              </label>
              {#if cur[0]}
                <label class="field">
                  <span>O quê</span>
                  <select value={cur[1]} onchange={(e) => ctl.pickChoice(cur[0], e.currentTarget.value)}>
                    {#if !cur[1]}<option value="" disabled>?</option>{/if}
                    {#each items as c (c.id)}<option value={c.id}>{c.label}</option>{/each}
                  </select>
                </label>
              {/if}
              {#if when}
                <label class="field">
                  <span>{when[0] === 'CURVA' ? 'Curva' : 'Quando'}</span>
                  <select value={when[2]} onchange={(e) => ctl.setWhen(Number(e.currentTarget.value))}>
                    {#each when[1] as w, n (n)}<option value={n}>{w}</option>{/each}
                  </select>
                </label>
              {/if}
            </div>
            {#if l.action === C.PARAM && prm}
              {@const hint = `${comma(C.paramFmt(prm, prm.min))} a ${comma(C.paramFmt(prm, prm.max))}`}
              <div class="row">
                {@render number(lo, comma(C.paramFmt(prm, l.off)), hint, setEnd('off', prm))}
                <span class="arrow">»</span>
                {@render number(hi, comma(C.paramFmt(prm, l.on)), hint, setEnd('on', prm))}
              </div>
            {:else if l.action === C.MIDI}
              <div class="row">
                {@render number(C.isPc(l) ? 'PC' : 'CC', `${l.arg}`, '0 a 127', (v) => { if (l) l.arg = Math.round(clamp(v, 0, 127)); })}
                <label class="field">
                  <span>Canal</span>
                  <select value={l.slot & 15} onchange={(e) => { if (l) l.slot = Number(e.currentTarget.value); }}>
                    {#each Array.from({ length: 16 }, (_, n) => n) as n (n)}<option value={n}>{n + 1}</option>{/each}
                  </select>
                </label>
                {#if !C.isPc(l)}
                  {@render number(lo, `${Math.round(l.off)}`, '0 a 127', (v) => { if (l) l.off = Math.round(clamp(v, 0, 127)); })}
                  <span class="arrow">»</span>
                  {@render number(hi, `${Math.round(l.on)}`, '0 a 127', (v) => { if (l) l.on = Math.round(clamp(v, 0, 127)); })}
                {/if}
              </div>
            {:else if C.unset(l)}
              <p class="hint">Escolha para onde a ligação vai (sem isso ela não é gravada).</p>
            {/if}
          </div>
        {:else if ctl.own.length}
          <p class="hint pick">Clique numa ligação para escolher para onde ela vai.</p>
        {/if}
      </section>
    </div>
  {/if}
</div>

{#snippet number(title: string, value: string, hint: string, apply: (v: number) => void)}
  <label class="field">
    <span>{title}</span>
    {#key value}
      <input class="num" {value} defaultValue={value} onchange={(e) => num(e, apply)}
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
  .led.mod { background: var(--dim); box-shadow: none; border-radius: 2px; }
  .led.mod.live { background: var(--accent); box-shadow: 0 0 6px var(--accent); }
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
    display: flex; flex-wrap: wrap; gap: 14px 16px; align-items: flex-start; margin-top: 16px; padding: 14px 16px;
    border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line);
  }
  .steps { flex-basis: 100%; display: flex; gap: 4px; }
  .step { display: flex; flex-direction: column; align-items: center; gap: 4px; width: 42px; }
  .step .bar { position: relative; width: 100%; height: 44px; border-radius: 4px; background: var(--bg); overflow: hidden; }
  .step .bar i { position: absolute; left: 0; right: 0; bottom: 0; background: var(--accent); opacity: 0.8; }
  .step input {
    width: 100%; height: 28px; text-align: center; border-radius: 5px; background: var(--bg); border: 1px solid var(--border-off);
    outline: none; font: 500 12.5px var(--f-value); user-select: text;
  }
  .step input:focus { border-color: var(--accent); }
  .step small { font: 500 10.5px var(--f-value); color: var(--off); }

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
