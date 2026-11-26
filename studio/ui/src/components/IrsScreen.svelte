<!--
  The 20 user IR slots as the pedal's flash holds them: each with its response drawn from its own samples (waveod
  reads the flash for the PC, so this is what really plays), and a .wav going into one after a look at what the
  pedal keeps of it (1024 samples at 44.1 kHz) and at what the slot loses.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import { irs } from '../lib/irs.svelte';
  import { curvePath, response, topOf } from '../lib/irmath';

  $effect(() => { untrack(() => void irs.enter()); });

  const slots = $derived(irs.slots);
  const s = $derived(irs.slot);
  const d = $derived(irs.draft);
  const used = $derived(slots ? slots.filter((x) => x.used).length : 0);
  const curves = $derived(slots ? slots.map((x) => (x.samples ? response(x.samples, 96) : null)) : []);
  const big = $derived(s?.samples ? response(s.samples) : null);
  const fresh = $derived(d ? response(d.file.samples) : null);
  const old = $derived(d && slots?.[d.slot]?.samples ? response(slots[d.slot].samples!) : null);
  const bigTop = $derived(big ? topOf(big) : 0);
  const sheetTop = $derived(fresh ? Math.max(topOf(fresh), old ? topOf(old) : -999) : 0);
  const pct = (v: number) => `${(v * 100).toFixed(v > 0.999 ? 0 : 1).replace('.', ',')}%`;
  const num = (v: number, n = 1) => v.toFixed(n).replace('.', ',');

  /** the impulse itself, the first 300 samples (6.8 ms), as a line */
  function wave(x: number[], w: number, h: number) {
    const n = 300, peak = Math.max(...x.slice(0, n).map(Math.abs), 1e-9);
    return x.slice(0, n).map((v, i) => `${i ? 'L' : 'M'}${((i / (n - 1)) * w).toFixed(1)},${(h / 2 - (v / peak) * (h / 2 - 2)).toFixed(1)}`).join('');
  }

  let draftName = $state('');
  function startRename() {
    if (!s) return;
    draftName = s.name;
    irs.renaming = true;
  }
  function take(node: HTMLInputElement) { node.focus(); node.select(); }
  function renameKey(e: KeyboardEvent) {
    if (e.key === 'Enter') void irs.rename(draftName);
    else if (e.key === 'Escape') { e.stopPropagation(); irs.renaming = false; }
  }
  function key(e: KeyboardEvent) {
    if (e.key === 'F2' && !d && s?.used) { e.preventDefault(); startRename(); }
  }
  function sheetKey(e: KeyboardEvent) {
    if (e.key === 'Escape' && d && !d.busy) { e.stopPropagation(); irs.draft = null; }
  }
</script>

<svelte:window onkeydown={key} />

<div class="screen">
  <header class="tools">
    <div class="title">
      <h1>IRs</h1>
      <span class="sub">os 20 IRs de usuário · no pedal: bloco USER IR (e Cab / IR do waveod)</span>
    </div>
    {#if slots}<span class="count">{used} de 20 com IR</span>{/if}
  </header>

  {#if !slots}
    <p class="note">Lendo os IRs do pedal…</p>
  {:else}
    <div class="body">
      <div class="slots" role="listbox" aria-label="IRs de usuário">
        {#each slots as x, i (x.slot)}
          <button role="option" aria-selected={x.slot === irs.picked} class="slot" class:empty={!x.used}
                  class:picked={x.slot === irs.picked} onclick={() => { irs.picked = x.slot; irs.renaming = false; }}
                  ondblclick={() => { irs.picked = x.slot; void irs.choose(); }}>
            <span class="num">{x.slot + 1}</span>
            <span class="nm">{x.used ? x.name : 'vazio'}</span>
            {#if curves[i]}
              {@const c = curves[i]!}
              <svg class="spark" viewBox="0 0 120 26" preserveAspectRatio="none" aria-hidden="true">
                <path d={curvePath(c, 120, 26, topOf(c), 36)} />
              </svg>
            {/if}
          </button>
        {/each}
      </div>

      {#if s}
        <section class="detail" class:empty={!s.used} aria-label="IR {s.slot + 1}">
          <span class="label">IR {s.slot + 1}</span>
          {#if irs.renaming}
            <input class="rename" bind:value={draftName} maxlength="16" spellcheck="false" use:take onkeydown={renameKey}
                   onblur={() => (irs.renaming = false)} aria-label="Nome do IR (até 16 letras)" />
            <p class="hint">Enter grava · Esc cancela · até 16 letras</p>
          {:else}
            <h2>{s.used ? s.name : 'Vazio'}</h2>
          {/if}
          {#if big && s.samples}
            <div class="graph">
              <svg viewBox="0 0 1000 220" preserveAspectRatio="none" role="img" aria-label="Resposta em frequência">
                {#each [100, 1000, 10000] as f (f)}<line x1={(Math.log10(f / 20) / 3) * 1000} x2={(Math.log10(f / 20) / 3) * 1000} y1="0" y2="220" />{/each}
                {#each [1, 2, 3, 4, 5, 6, 7] as k (k)}<line x1="0" x2="1000" y1={(k * 220) / 8} y2={(k * 220) / 8} />{/each}
                <path class="curve" d={curvePath(big, 1000, 220, bigTop)} />
              </svg>
              <div class="axis"><span style:left="22.6%">100 Hz</span><span style:left="56%">1 kHz</span><span style:left="89.3%">10 kHz</span>
                <span class="db">{bigTop} dB</span><span class="db low">{bigTop - 48} dB</span></div>
            </div>
            <div class="imp">
              <span>Impulso, 6,8 ms</span>
              <svg viewBox="0 0 600 60" preserveAspectRatio="none" aria-hidden="true"><path d={wave(s.samples, 600, 60)} /></svg>
            </div>
            <p class="hint">1024 amostras a 44,1 kHz (23,2 ms), lidas da flash do pedal: é o que o bloco toca.</p>
          {:else}
            <p class="hint">Nada guardado aqui. Um .wav gravado neste slot vira um IR de 1024 amostras a 44,1 kHz (23,2 ms):
              o pedal guarda o começo da resposta, o que basta para um gabinete.</p>
          {/if}
          <div class="acts">
            <button class={s.used ? 'warn' : 'primary'} onclick={() => irs.choose()}>
              <Icon name="file" size={16} stroke={2.1} /> {s.used ? 'Substituir por um .wav…' : 'Gravar um .wav…'}
            </button>
            {#if s.used}<button onclick={startRename} title="F2"><Icon name="pencil" size={15} stroke={2} /> Renomear</button>{/if}
          </div>
        </section>
      {/if}
    </div>
  {/if}
</div>

{#if d && fresh}
  {@const was = slots?.[d.slot]}
  <div class="veil" role="presentation" onclick={() => !d.busy && (irs.draft = null)}></div>
  <div class="sheet" role="dialog" aria-modal="true" aria-labelledby="ir-title" tabindex="-1" onkeydown={sheetKey}>
    <h3 id="ir-title">Gravar no IR {d.slot + 1}?</h3>
    <p class="file"><Icon name="file" size={15} stroke={2} /> {d.file.file} · {d.file.fmt} {d.file.bits} bits · {num(d.file.rate / 1000)} kHz
      · {d.file.channels === 1 ? 'mono' : `${d.file.channels} canais (vai o primeiro)`} · {num(d.file.ms)} ms</p>
    <div class="graph small">
      <svg viewBox="0 0 1000 160" preserveAspectRatio="none" role="img" aria-label="O IR novo sobre o atual">
        {#each [100, 1000, 10000] as f (f)}<line x1={(Math.log10(f / 20) / 3) * 1000} x2={(Math.log10(f / 20) / 3) * 1000} y1="0" y2="160" />{/each}
        {#if old}<path class="was" d={curvePath(old, 1000, 160, sheetTop)} />{/if}
        <path class="curve" d={curvePath(fresh, 1000, 160, sheetTop)} />
      </svg>
      <div class="legend"><span class="k new"></span>o novo{#if old}<span class="k old"></span>o que o slot tem agora{/if}</div>
    </div>
    <dl class="facts">
      <div><dt>Fica no pedal</dt><dd>{pct(d.file.kept)} da energia</dd></div>
      <div><dt>Conversão</dt><dd>{[d.file.resampled ? `reamostrado de ${num(d.file.rate / 1000)} kHz` : '', d.file.cut ? 'cortado em 23,2 ms' : '',
                                   ].filter(Boolean).join(', ') || 'nenhuma'}</dd></div>
      <div><dt>Pico</dt><dd>{num(d.file.peak_db)} dBFS{d.file.normalized ? ` · ${d.file.gain_db >= 0 ? '+' : ''}${num(d.file.gain_db)} dB` : ''}</dd></div>
    </dl>
    <div class="seg" role="radiogroup" aria-label="Nível">
      <button role="radio" aria-checked={!d.normalize} class:on={!d.normalize} onclick={() => irs.normalize(false)}>Nível do arquivo</button>
      <button role="radio" aria-checked={d.normalize} class:on={d.normalize} onclick={() => irs.normalize(true)}>Igualar aos gabinetes de fábrica</button>
    </div>
    {#if d.file.clipped}<p class="lost">{d.file.clipped} amostras passam do máximo e serão cortadas.</p>{/if}
    <label class="name"><span>Nome no pedal (até 16 letras)</span>
      <input bind:value={d.name} maxlength="16" spellcheck="false" use:take />
    </label>
    <p class="lost" class:safe={!was?.used}>
      {#if !was?.used}O slot está vazio.
      {:else}Isto substitui o IR {d.slot + 1}, <b>{was.name}</b>. Os presets que usam este slot passam a tocar o novo.{/if}
    </p>
    <footer>
      <button onclick={() => (irs.draft = null)} disabled={d.busy}>Cancelar</button>
      <button class={was?.used ? 'danger' : 'primary'} disabled={d.busy || !d.name.trim()} onclick={() => irs.write()}>
        {d.busy ? 'Gravando…' : `${was?.used ? 'Substituir' : 'Gravar'} no IR ${d.slot + 1}`}</button>
    </footer>
  </div>
{/if}

<style>
  .screen { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .tools { display: flex; align-items: center; gap: 18px; padding: 16px 24px 14px; border-bottom: 1px solid var(--line); }
  .title { display: flex; align-items: baseline; gap: 12px; flex: 1; min-width: 0; }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  .sub { font-size: 13px; color: var(--label); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .count { font: 500 12.5px var(--f-value); color: var(--label); white-space: nowrap; }
  .note { margin: 24px; color: var(--label); }
  .body { flex: 1; min-height: 0; display: flex; }
  .slots {
    flex: none; width: 520px; display: grid; grid-template-columns: repeat(2, 1fr); gap: 8px; align-content: start;
    padding: 18px 20px; overflow-y: auto; border-right: 1px solid var(--line); background: var(--surface);
  }
  .slot {
    display: grid; grid-template-columns: 28px 1fr; grid-template-rows: auto 26px; column-gap: 8px; row-gap: 2px; align-items: center;
    height: 66px; padding: 6px 12px; border-radius: var(--r-md); border: 1px solid var(--line); background: var(--bg); text-align: left;
  }
  .slot:hover { border-color: var(--border-off); }
  .slot.picked { border-color: var(--label); background: var(--raised); }
  .slot.empty { border-style: dashed; background: transparent; }
  .num { grid-row: 1 / 3; font: 500 18px var(--f-value); color: var(--accent); }
  .empty .num { color: var(--dim); }
  .nm { font: 600 15px var(--f-display); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .empty .nm { color: var(--dim); font-weight: 500; grid-row: 1 / 3; }
  .spark { width: 100%; height: 26px; }
  .spark path { fill: none; stroke: var(--accent); stroke-width: 1.5; vector-effect: non-scaling-stroke; opacity: 0.85; }
  .detail { flex: 1; min-width: 0; overflow-y: auto; padding: 24px 28px; }
  .label { font: 500 14px var(--f-value); color: var(--accent); }
  h2 { margin: 4px 0 14px; font: 700 32px/1.1 var(--f-display); overflow-wrap: anywhere; }
  .empty h2 { color: var(--off); }
  .rename {
    display: block; width: min(420px, 100%); height: 44px; margin: 4px 0 4px; padding: 0 10px; border-radius: var(--r-sm);
    background: var(--bg); border: 1px solid var(--accent); outline: none; font: 700 26px var(--f-display); user-select: text;
  }
  .graph { position: relative; max-width: 900px; }
  .graph svg { display: block; width: 100%; height: 220px; border-radius: 8px; background: var(--bg); }
  .graph.small svg { height: 160px; }
  .graph line { stroke: var(--line); stroke-width: 1; vector-effect: non-scaling-stroke; }
  .graph .curve { fill: none; stroke: var(--accent); stroke-width: 2.5; vector-effect: non-scaling-stroke; }
  .graph .was { fill: none; stroke: var(--label); stroke-width: 1.5; stroke-dasharray: 5 4; vector-effect: non-scaling-stroke; }
  .axis { position: relative; height: 18px; font: 500 11px var(--f-value); color: var(--off); }
  .axis span { position: absolute; top: 3px; transform: translateX(-50%); white-space: nowrap; }
  .axis .db { right: 0; left: auto !important; transform: none; top: -220px; padding: 4px 6px; }
  .axis .db.low { top: -20px; }
  .legend { display: flex; align-items: center; gap: 6px; margin: 6px 0 2px; font-size: 12.5px; color: var(--label); }
  .k { width: 18px; height: 0; border-top: 2.5px solid var(--accent); margin-left: 8px; }
  .k:first-child { margin-left: 0; }
  .k.old { border-top: 1.5px dashed var(--label); }
  .imp { display: flex; align-items: center; gap: 12px; max-width: 900px; margin: 10px 0 12px; }
  .imp span { flex: none; font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .imp svg { flex: 1; height: 60px; border-radius: 6px; background: var(--bg); }
  .imp path { fill: none; stroke: var(--label); stroke-width: 1.2; vector-effect: non-scaling-stroke; }
  .hint { margin: 0 0 16px; max-width: 620px; font-size: 13.5px; line-height: 1.55; color: var(--label); }
  .acts { display: flex; gap: 8px; flex-wrap: wrap; }
  .acts button, .sheet footer button {
    display: flex; align-items: center; gap: 7px; height: 36px; padding: 0 14px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); font: 600 15px var(--f-display);
  }
  .acts button:hover { background: var(--raised); }
  .primary { background: var(--accent) !important; color: var(--accent-ink); border-color: var(--accent) !important; font-weight: 700 !important; }
  .primary:hover:not(:disabled) { background: #f7c169 !important; }
  .warn { color: #f6c15a; }
  .warn:hover { background: #2a1f0c !important; border-color: #5a4114 !important; }

  .veil { position: fixed; inset: 0; z-index: 60; background: rgba(5, 5, 4, 0.62); }
  .sheet {
    position: fixed; z-index: 61; left: 50%; top: 48%; transform: translate(-50%, -50%); width: min(640px, calc(100vw - 48px));
    max-height: calc(100vh - 40px); overflow-y: auto;
    padding: 20px 22px 18px; border-radius: 14px; background: var(--raised); border: 1px solid var(--border-off); box-shadow: var(--float-shadow);
  }
  .sheet:focus { outline: none; }
  .sheet h3 { margin: 0 0 6px; font: 700 22px var(--f-display); }
  .file { display: flex; align-items: center; gap: 6px; flex-wrap: wrap; margin: 0 0 12px; font: 500 12.5px var(--f-value); color: var(--label); }
  .facts { display: flex; gap: 28px; flex-wrap: wrap; margin: 10px 0 12px; }
  .facts dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .facts dd { margin: 3px 0 0; font: 500 13.5px var(--f-value); }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); margin-bottom: 12px; }
  .seg button { flex: 1; height: 30px; border-radius: 7px; font: 600 14px var(--f-display); color: var(--label); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  .name { display: flex; flex-direction: column; gap: 6px; margin-bottom: 12px; }
  .name span { font: 600 12px var(--f-display); letter-spacing: 0.06em; text-transform: uppercase; color: var(--label); }
  .name input { height: 36px; padding: 0 10px; border-radius: var(--r-sm); background: var(--bg); border: 1px solid var(--border-off); outline: none; font: 600 17px var(--f-display); user-select: text; }
  .name input:focus { border-color: var(--accent); }
  .lost { margin: 6px 0 14px; font-size: 14px; line-height: 1.5; color: #f6dcae; }
  .lost.safe { color: var(--label); }
  .lost b { font-weight: 600; color: var(--text); }
  .sheet footer { display: flex; justify-content: flex-end; gap: 8px; }
  .sheet footer button:hover:not(:disabled) { background: var(--track); }
  .sheet footer button:disabled { opacity: 0.5; cursor: default; }
  .danger { background: #3a1716 !important; color: #ffd6d4; border-color: #6b2523 !important; }
  .danger:hover:not(:disabled) { background: #4a1c1b !important; }
  @media (max-width: 1240px) { .slots { width: 380px; grid-template-columns: 1fr; } }
</style>
