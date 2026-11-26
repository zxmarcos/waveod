<!--
  The global EQ as the pedal's EQ screen draws it (scr_eq.c): the response over 20 Hz .. 20 kHz and +-18 dB, a
  handle per stage at its frequency and gain (drag it: frequency sideways, gain up and down; a stage that is off
  comes on), the eight stages in a strip, the picked stage's frequency / Q / gain, the level, FLAT / VOLTAR.
-->
<script lang="ts">
  import { EQ_LEVEL, STAGES, hzText, responseDb } from '../lib/globalset';
  import { globals } from '../lib/globals.svelte';

  const H = 230, DB = 18, LO = Math.log10(20), HI = Math.log10(20000), PAD = 16;
  let W = $state(1000);                       // the graph's width in pixels: the handles stay round
  const x = (hz: number) => PAD + ((Math.log10(Math.min(Math.max(hz, 20), 20000)) - LO) / (HI - LO)) * (W - 2 * PAD);
  const hzAt = (px: number) => Math.pow(10, LO + ((px - PAD) / (W - 2 * PAD)) * (HI - LO));
  const y = (db: number) => H / 2 - (Math.max(-DB, Math.min(DB, db)) / DB) * (H / 2 - 8);
  const dbAt = (py: number) => ((H / 2 - py) / (H / 2 - 8)) * DB;

  const p = $derived(globals.eq.map((s) => s.value));
  const on = $derived(globals.value('eq_on') === 1);
  const st = $derived(STAGES[globals.stage]);
  const FREQS = Array.from({ length: 160 }, (_, i) => Math.pow(10, LO + (i / 159) * (HI - LO)));

  function curve(stage?: number) {
    return FREQS.map((f, i) => `${i ? 'L' : 'M'}${x(f).toFixed(1)},${y(responseDb(p, f, stage)).toFixed(1)}`).join('');
  }
  const total = $derived(p.length ? curve() : '');
  const area = $derived(total ? `${total}L${x(20000)},${H / 2}L${x(20)},${H / 2}Z` : '');
  const mine = $derived(p.length ? curve(globals.stage) : '');
  const handleY = (i: number) => {
    const s = STAGES[i];
    return y(s.g >= 0 ? p[s.g] : responseDb(p, p[s.f], i));
  };

  // ---------------------------------------------------------------- dragging a handle
  let svg = $state<SVGSVGElement>();
  let drag = -1;
  function point(e: PointerEvent) {
    const r = svg!.getBoundingClientRect();
    return [((e.clientX - r.left) / r.width) * W, ((e.clientY - r.top) / r.height) * H];
  }
  function down(e: PointerEvent, i: number) {
    e.preventDefault();
    globals.stage = i;
    drag = i;
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    if (!p[STAGES[i].on]) globals.setEq(STAGES[i].on, 1);
  }
  function move(e: PointerEvent) {
    if (drag < 0) return;
    const [px, py] = point(e), s = STAGES[drag];
    globals.setEq(s.f, Math.round(hzAt(px)));
    if (s.g >= 0) globals.setEq(s.g, Math.round(dbAt(py)));
  }
  function up() { drag = -1; }

  const fmt = (k: number) => {
    const v = p[k];
    if (k === st.f) return hzText(v);
    if (k === st.q) return v.toFixed(2).replace('.', ',');
    return `${v > 0 ? '+' : ''}${Math.round(v)} dB`;
  };
  const cells = $derived([
    { k: st.f, label: 'Frequência' }, { k: st.q, label: 'Largura (Q)' }, ...(st.g >= 0 ? [{ k: st.g, label: 'Ganho' }] : []),
  ]);
  /** frequency sliders move on a log scale */
  const toSlider = (k: number, v: number) => (k === st.f ? Math.log10(v) : v);
  const fromSlider = (k: number, v: number) => (k === st.f ? Math.round(Math.pow(10, v)) : v);
</script>

<section class="eq" class:off={!on} aria-label="EQ global">
  <header>
    <h2>EQ global</h2>
    <span class="sub">depois da cadeia, em todos os presets</span>
    <div class="seg" role="radiogroup" aria-label="EQ global ligado">
      <button role="radio" aria-checked={!on} class:on={!on} onclick={() => globals.set('eq_on', 0)}>Desligado</button>
      <button role="radio" aria-checked={on} class:on onclick={() => globals.set('eq_on', 1)}>Ligado</button>
    </div>
    {#if globals.canUndoFlat}
      <button class="ghost" onclick={() => globals.undoFlat()}>Voltar</button>
    {:else}
      <button class="ghost" onclick={() => globals.flatten()} title="Todos os ganhos em 0 dB, os cortes desligados, o nível em 50">Flat</button>
    {/if}
  </header>

  <svg bind:this={svg} bind:clientWidth={W} viewBox="0 0 {W} {H}" class="graph" role="img"
       aria-label="Resposta do EQ global" onpointermove={move} onpointerup={up} onpointercancel={up}>
    {#each [50, 100, 200, 500, 1000, 2000, 5000, 10000] as f (f)}
      <line class="grid" x1={x(f)} x2={x(f)} y1="0" y2={H} />
    {/each}
    {#each [-12, -6, 6, 12] as d (d)}<line class="grid" x1="0" x2={W} y1={y(d)} y2={y(d)} />{/each}
    <line class="zero" x1="0" x2={W} y1={y(0)} y2={y(0)} />
    {#if p.length}
      <path class="area" d={area} />
      <path class="mine" d={mine} />
      <path class="total" d={total} />
      {#each STAGES as s, i (s.short)}
        <g class="handle" class:sel={globals.stage === i} class:dim={!p[s.on]} role="slider" tabindex="-1"
           aria-label={s.name} aria-valuenow={p[s.f]} onpointerdown={(e) => down(e, i)}>
          <circle cx={x(p[s.f])} cy={handleY(i)} r="13" />
          <text x={x(p[s.f])} y={handleY(i) + 4}>{i < 2 ? ['L', 'LS'][i] : i < 6 ? i - 1 : ['HS', 'H'][i - 6]}</text>
        </g>
      {/each}
    {/if}
  </svg>
  <div class="axis" aria-hidden="true">
    {#each [[100, '100'], [1000, '1k'], [10000, '10k']] as [f, t] (f)}<span style:left="{x(Number(f))}px">{t}</span>{/each}
  </div>

  <div class="strip" role="tablist" aria-label="Estágios do EQ">
    {#each STAGES as s, i (s.short)}
      <button role="tab" aria-selected={globals.stage === i} class:sel={globals.stage === i} class:lit={!!p[s.on]}
              onclick={() => (globals.stage = i)} ondblclick={() => globals.setEq(s.on, p[s.on] ? 0 : 1)}
              title="Duplo clique liga / desliga">
        <i></i>{s.short}
      </button>
    {/each}
  </div>

  {#if p.length}
    <div class="params">
      <div class="stage">
        <b>{st.name}</b>
        <button class="pow" class:lit={!!p[st.on]} onclick={() => globals.setEq(st.on, p[st.on] ? 0 : 1)}>
          {p[st.on] ? 'Ligado' : 'Desligado'}
        </button>
      </div>
      {#each cells as c (c.k)}
        {@const r = globals.eq[c.k]}
        <label class="cell">
          <span>{c.label}</span>
          <b>{fmt(c.k)}</b>
          <input type="range" min={toSlider(c.k, r.min)} max={toSlider(c.k, r.max)}
                 step={c.k === st.f ? 0.002 : r.step} value={toSlider(c.k, r.value)}
                 oninput={(e) => globals.setEq(c.k, fromSlider(c.k, Number(e.currentTarget.value)))} />
        </label>
      {/each}
      <label class="cell level">
        <span>Nível</span>
        <b>{Math.round(p[EQ_LEVEL])}</b>
        <input type="range" min="0" max="100" step="1" value={p[EQ_LEVEL]}
               oninput={(e) => globals.setEq(EQ_LEVEL, Number(e.currentTarget.value))} />
      </label>
    </div>
  {/if}
</section>

<style>
  .eq { padding: 18px 20px 20px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); }
  header { display: flex; align-items: center; gap: 12px; margin-bottom: 12px; flex-wrap: wrap; }
  h2 { margin: 0; font: 700 20px var(--f-display); }
  .sub { margin-right: auto; font-size: 12.5px; color: var(--label); }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--bg); border: 1px solid var(--line); }
  .seg button { height: 28px; padding: 0 12px; border-radius: 7px; font: 600 14px var(--f-display); color: var(--label); }
  .seg button:hover:not(.on) { color: var(--text); background: var(--track); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  .ghost { height: 34px; padding: 0 14px; border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 600 14.5px var(--f-display); }
  .ghost:hover { background: var(--raised); }

  .graph { display: block; width: 100%; height: 230px; border-radius: 8px; background: var(--bg); touch-action: none; }
  .grid { stroke: var(--line); stroke-width: 1; vector-effect: non-scaling-stroke; }
  .zero { stroke: var(--wire); stroke-width: 1; vector-effect: non-scaling-stroke; }
  .area { fill: var(--accent); opacity: 0.12; }
  .mine { fill: none; stroke: var(--label); stroke-width: 1.5; stroke-dasharray: 4 4; vector-effect: non-scaling-stroke; opacity: 0.7; }
  .total { fill: none; stroke: var(--accent); stroke-width: 2.5; vector-effect: non-scaling-stroke; }
  .off .area { opacity: 0.04; }
  .off .total { stroke: var(--off); }
  .handle { cursor: grab; }
  .handle circle { fill: var(--raised); stroke: var(--label); stroke-width: 1.5; vector-effect: non-scaling-stroke; }
  .handle text { font: 600 12px var(--f-display); fill: var(--text); text-anchor: middle; pointer-events: none; }
  .handle.dim circle { stroke: var(--dim); fill: var(--bg); }
  .handle.dim text { fill: var(--off); }
  .handle.sel circle { stroke: var(--accent); stroke-width: 2.5; fill: #2a2212; }
  .axis { position: relative; height: 16px; font: 500 11px var(--f-value); color: var(--off); }
  .axis span { position: absolute; top: 2px; transform: translateX(-50%); }

  .strip { display: grid; grid-template-columns: repeat(8, 1fr); gap: 4px; margin-top: 8px; }
  .strip button {
    display: flex; align-items: center; justify-content: center; gap: 6px; height: 32px; border-radius: var(--r-sm);
    border: 1px solid var(--line); font: 600 13.5px var(--f-display); color: var(--label);
  }
  .strip button:hover { background: var(--raised); }
  .strip button.sel { border-color: var(--accent); color: var(--text); }
  .strip i { width: 6px; height: 6px; border-radius: 50%; background: var(--dim); }
  .strip .lit i { background: var(--accent); box-shadow: 0 0 5px var(--accent); }

  .params { display: flex; flex-wrap: wrap; gap: 12px 22px; align-items: flex-end; margin-top: 14px; }
  .stage { display: flex; flex-direction: column; gap: 6px; min-width: 150px; }
  .stage b { font: 700 16px var(--f-display); }
  .pow { align-self: flex-start; height: 28px; padding: 0 10px; border-radius: var(--r-sm); border: 1px solid var(--border-off);
         font: 600 13px var(--f-display); color: var(--label); }
  .pow.lit { color: var(--accent); border-color: #5a4114; }
  .cell { display: grid; grid-template-columns: 1fr auto; gap: 2px 10px; width: 210px; }
  .cell span { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .cell b { font: 500 13.5px var(--f-value); text-align: right; }
  .cell input { grid-column: 1 / -1; }
  .cell.level { width: 160px; }
  input[type='range'] { width: 100%; accent-color: var(--accent); height: 22px; cursor: pointer; }
</style>
