<!--
  The virtual mic of a Cab / IR block (waveod's engine), as the pedal's MIC screen shows it but with room to move
  it: the speaker from the front (where on the cone: POSITION), from the side (how far from the grille: DISTANCE,
  at what height, tilted by ANGLE), the mic type, and the change it makes in the response against the IR as
  captured. Every move is sent at once, like a knob.
-->
<script lang="ts">
  import Icon from './Icon.svelte';
  import { studio } from '../lib/store.svelte';
  import * as M from '../lib/cabmic';

  let { onclose }: { onclose: () => void } = $props();

  const blk = $derived(studio.selectedBlock);
  const P = $derived({
    position: studio.params.find((p) => p.knob === 8), distance: studio.params.find((p) => p.knob === 9),
    angle: studio.params.find((p) => p.knob === 10), type: studio.params.find((p) => p.knob === 11),
  });
  const mic = $derived<M.Mic>({ position: P.position?.value ?? 0, distance: P.distance?.value ?? 0,
                                angle: P.angle?.value ?? 0, type: P.type?.value ?? 0 });
  const st = $derived(M.step(mic.position, M.POSITIONS));
  const sd = $derived(M.step(mic.distance, M.DISTANCES));
  const t = $derived(M.micType(mic.type));
  const typeNames = $derived(P.type?.options.length ? P.type.options : ['Original', ...Array.from({ length: M.TYPES }, (_, i) => `${i + 1}`)]);

  function set(which: keyof typeof P, v: number) {
    const p = P[which];
    if (p) studio.setParam(p.k, v);
  }
  function neutral() {
    set('position', 0);
    set('distance', 0);
    set('angle', 0);
  }

  // ---------------------------------------------------------------- the front view: viewBox -80..80
  let front = $state<SVGSVGElement>();
  let dir = $state(0);                                   // where on the circle the mic is drawn (only the look)
  const fr = $derived(M.stepRadius(st));
  function frontAt(e: PointerEvent) {
    const r = front!.getBoundingClientRect();
    const x = ((e.clientX - r.left) / r.width) * 160 - 80, y = ((e.clientY - r.top) / r.height) * 160 - 80;
    dir = Math.atan2(y, x);
    set('position', Math.round(M.knob(M.radiusStep(Math.hypot(x, y)), M.POSITIONS)));
  }

  // ---------------------------------------------------------------- the side view: x 0..300, y -118..80 (room above
  // for a mic at the cone's edge tilted 45 degrees)
  const TOP = 118, VH = 198;
  const GX = 64, D0 = 10, DW = 170, BODY = 64;
  let side = $state<SVGSVGElement>();
  let grab: 'tip' | 'tail' | null = null;
  const tipX = $derived(GX + D0 + (sd / (M.DISTANCES - 1)) * DW);
  const tipY = $derived(-fr);
  const an = $derived((mic.angle * Math.PI) / 180);
  const tailX = $derived(tipX + BODY * Math.cos(an));
  const tailY = $derived(tipY - BODY * Math.sin(an));
  function sidePoint(e: PointerEvent) {
    const r = side!.getBoundingClientRect();
    return [((e.clientX - r.left) / r.width) * 300, ((e.clientY - r.top) / r.height) * VH - TOP];
  }
  function sideAt(e: PointerEvent) {
    const [x, y] = sidePoint(e);
    if (grab === 'tail') {
      const deg = (Math.atan2(-(y - tipY), x - tipX) * 180) / Math.PI;
      set('angle', Math.round(Math.max(0, Math.min(45, deg))));
      return;
    }
    const s = Math.max(0, Math.min(M.DISTANCES - 1, ((x - GX - D0) / DW) * (M.DISTANCES - 1)));
    set('distance', Math.round(M.knob(s, M.DISTANCES)));
    set('position', Math.round(M.knob(M.radiusStep(Math.abs(y)), M.POSITIONS)));
  }
  function down(e: PointerEvent, view: 'front' | 'side') {
    if (e.button !== 0) return;
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    studio.dragging = true;
    if (view === 'side') {
      const [x, y] = sidePoint(e);
      grab = Math.hypot(x - tailX, y - tailY) < 16 ? 'tail' : 'tip';
      sideAt(e);
    } else {
      grab = 'tip';
      frontAt(e);
    }
  }
  function move(e: PointerEvent, view: 'front' | 'side') {
    if (!grab) return;
    if (view === 'side') sideAt(e);
    else frontAt(e);
  }
  function up() {
    grab = null;
    studio.dragging = false;
  }

  // ---------------------------------------------------------------- the response change: 50 Hz .. 16 kHz, +-12 dB
  const CW = 1000, CH = 150, RANGE = 12;
  const cx = (hz: number) => (Math.log10(hz / 50) / Math.log10(16000 / 50)) * CW;
  const cy = (db: number) => CH / 2 - (Math.max(-RANGE, Math.min(RANGE, db)) / RANGE) * (CH / 2 - 4);
  const change = $derived.by(() => {
    const db = M.curve(mic);
    return Array.from({ length: 120 }, (_, i) => {
      const hz = 50 * Math.pow(16000 / 50, i / 119);
      return `${i ? 'L' : 'M'}${cx(hz).toFixed(1)},${cy(M.dbAt(db, hz)).toFixed(1)}`;
    }).join('');
  });
  const signed = (v: number) => `${v > 0 ? '+' : ''}${Math.round(v)}`;
  function key(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      e.stopPropagation();
      onclose();
    }
  }
  function take(node: HTMLElement) { node.focus(); }
</script>

<div class="veil" role="presentation" onclick={onclose}></div>
<div class="sheet" role="dialog" aria-modal="true" aria-labelledby="mic-title" tabindex="-1" onkeydown={key} use:take>
  <header>
    <div class="title">
      <span class="kicker">Microfone virtual</span>
      <h3 id="mic-title">{blk?.name ?? ''}</h3>
    </div>
    <div class="seg" role="radiogroup" aria-label="Tipo do microfone">
      {#each typeNames as name, n (n)}
        <button role="radio" aria-checked={t === n} class:on={t === n} onclick={() => set('type', n)}>{name}</button>
      {/each}
    </div>
    <button class="ghost" onclick={neutral} title="Posição, distância e ângulo em 0: o som do IR como foi capturado">
      <Icon name="loop" size={15} stroke={2} /> Onde o IR foi capturado
    </button>
    <button class="close" onclick={onclose} aria-label="Fechar (Esc)"><Icon name="close" size={18} stroke={2.2} /></button>
  </header>

  <div class="views">
    <figure class="front">
      <figcaption>De frente <span>arraste: do centro à borda do cone</span></figcaption>
      <svg bind:this={front} viewBox="-80 -80 160 160" role="slider" tabindex="-1" aria-label="Posição no cone"
           aria-valuenow={mic.position} onpointerdown={(e) => down(e, 'front')} onpointermove={(e) => move(e, 'front')}
           onpointerup={up} onpointercancel={up}>
        <circle r={M.R_SURROUND} class="surround" />
        <circle r={M.R_CONE} class="cone" />
        {#each [48, 34] as r (r)}<circle r={r} class="ring" />{/each}
        <circle r={M.R_CAP} class="cap" />
        <line x1="0" y1="0" x2={fr * Math.cos(dir)} y2={fr * Math.sin(dir)} class="ray" />
        <circle cx={fr * Math.cos(dir)} cy={fr * Math.sin(dir)} r="8" class="mic" />
        <circle cx={fr * Math.cos(dir)} cy={fr * Math.sin(dir)} r="2.6" class="dot" />
      </svg>
    </figure>
    <figure class="side">
      <figcaption>De lado <span>arraste o microfone: distância e altura · a alça de trás: o ângulo</span></figcaption>
      <svg bind:this={side} viewBox="0 {-TOP} 300 {VH}" role="slider" tabindex="-1" aria-label="Distância e ângulo"
           aria-valuenow={mic.distance} onpointerdown={(e) => down(e, 'side')} onpointermove={(e) => move(e, 'side')}
           onpointerup={up} onpointercancel={up}>
        <line x1={GX - 4} y1="-70" x2={GX - 30} y2={-M.R_CAP} class="spk" />
        <line x1={GX - 4} y1="70" x2={GX - 30} y2={M.R_CAP} class="spk" />
        <line x1={GX - 30} y1={-M.R_CAP} x2={GX - 30} y2={M.R_CAP} class="spk" />
        <path d="M{GX - 30} {-M.R_CAP} L{GX - 22} 0 L{GX - 30} {M.R_CAP}" class="capside" />
        <line x1={GX} y1="-76" x2={GX} y2="76" class="grille" />
        <line x1={GX} y1="0" x2="300" y2="0" class="axis" />
        {#each Array.from({ length: M.DISTANCES }, (_, i) => i) as i (i)}
          <line x1={GX + D0 + (i / (M.DISTANCES - 1)) * DW} x2={GX + D0 + (i / (M.DISTANCES - 1)) * DW} y1="74" y2="78" class="tick" />
        {/each}
        {#if t === 1}
          <line x1={tipX} y1={tipY} x2={tipX + (BODY + 8) * Math.cos(an)} y2={tipY - (BODY + 8) * Math.sin(an)} class="body slim" />
          <line x1={tipX} y1={tipY} x2={tipX + 18 * Math.cos(an)} y2={tipY - 18 * Math.sin(an)} class="head" />
        {:else}
          <line x1={tipX} y1={tipY} x2={tailX} y2={tailY} class="body" />
        {/if}
        <line x1={tipX} y1={tipY} x2={tipX + 5 * Math.cos(an)} y2={tipY - 5 * Math.sin(an)} class="tip" />
        <circle cx={tailX} cy={tailY} r="6" class="tail" />
      </svg>
    </figure>
  </div>

  <div class="values">
    {#each [
      { which: 'position', name: 'Posição', word: M.positionWord(st), v: mic.position, min: -100, max: 100, unit: '' },
      { which: 'distance', name: 'Distância', word: M.distanceWord(sd), v: mic.distance, min: -100, max: 100, unit: '' },
      { which: 'angle', name: 'Ângulo', word: mic.angle ? 'inclinado' : 'no eixo', v: mic.angle, min: 0, max: 45, unit: '°' },
    ] as x (x.which)}
      <label class="val">
        <span class="n">{x.name}</span>
        <b>{x.word} <em>{x.unit ? `${Math.round(x.v)}${x.unit}` : signed(x.v)}</em></b>
        <input type="range" min={x.min} max={x.max} step="1" value={x.v}
               oninput={(e) => set(x.which as keyof typeof P, Number(e.currentTarget.value))} />
      </label>
    {/each}
  </div>

  <figure class="curve">
    <figcaption>O que muda no som <span>em relação ao IR como foi capturado</span></figcaption>
    <svg viewBox="0 0 {CW} {CH}" preserveAspectRatio="none" role="img" aria-label="Mudança na resposta">
      {#each [100, 1000, 10000] as f (f)}<line x1={cx(f)} x2={cx(f)} y1="0" y2={CH} class="grid" />{/each}
      {#each [-6, 6] as d (d)}<line x1="0" x2={CW} y1={cy(d)} y2={cy(d)} class="grid" />{/each}
      <line x1="0" x2={CW} y1={cy(0)} y2={cy(0)} class="zero" />
      <path d={change} class="change" />
    </svg>
    <div class="axis"><span style:left="{(cx(100) / CW) * 100}%">100 Hz</span><span style:left="{(cx(1000) / CW) * 100}%">1 kHz</span>
      <span style:left="{(cx(10000) / CW) * 100}%">10 kHz</span><span class="db">+12 dB</span><span class="db low">−12 dB</span></div>
  </figure>
</div>

<style>
  .veil { position: fixed; inset: 0; z-index: 60; background: rgba(5, 5, 4, 0.62); }
  .sheet {
    position: fixed; z-index: 61; left: 50%; top: 50%; transform: translate(-50%, -50%);
    width: min(1040px, calc(100vw - 48px)); max-height: calc(100vh - 32px); overflow-y: auto;
    padding: 18px 22px 20px; border-radius: 14px; background: var(--raised); border: 1px solid var(--border-off);
    box-shadow: var(--float-shadow);
  }
  .sheet:focus { outline: none; }
  header { display: flex; align-items: center; gap: 12px; margin-bottom: 14px; }
  .title { display: flex; flex-direction: column; margin-right: auto; min-width: 0; }
  .kicker { font: 600 12px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--accent); }
  h3 { margin: 2px 0 0; font: 700 24px/1.1 var(--f-display); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--bg); border: 1px solid var(--line); }
  .seg button { height: 30px; padding: 0 12px; border-radius: 7px; font: 600 14px var(--f-display); color: var(--label); white-space: nowrap; }
  .seg button:hover:not(.on) { color: var(--text); background: var(--track); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  .ghost { display: flex; align-items: center; gap: 6px; height: 36px; padding: 0 12px; border-radius: var(--r-sm);
           border: 1px solid var(--border-off); font: 600 14px var(--f-display); white-space: nowrap; }
  .ghost:hover { background: var(--track); }
  .close { display: grid; place-items: center; width: 36px; height: 36px; border-radius: var(--r-sm); color: var(--label); }
  .close:hover { background: var(--track); color: var(--text); }

  .views { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.75fr); gap: 14px; }
  figure { margin: 0; }
  figcaption { margin: 0 0 6px; font: 600 12.5px var(--f-display); letter-spacing: 0.06em; text-transform: uppercase; color: var(--label); }
  figcaption span { margin-left: 8px; font: 400 12px var(--f-body); letter-spacing: 0; text-transform: none; color: var(--off); }
  .views svg { display: block; width: 100%; border-radius: 10px; background: var(--bg); touch-action: none; cursor: crosshair; }
  .front svg { aspect-ratio: 1; max-height: 320px; }
  .side svg { aspect-ratio: 300 / 198; max-height: 320px; }
  .surround { fill: var(--line); stroke: var(--wire); stroke-width: 2; }
  .cone { fill: var(--track); stroke: var(--border-off); stroke-width: 1; }
  .ring { fill: none; stroke: var(--line); stroke-width: 1; }
  .cap { fill: var(--border-off); stroke: var(--wire); stroke-width: 1; }
  .ray { stroke: var(--off); stroke-width: 1; }
  .mic { fill: var(--accent); stroke: var(--bg); stroke-width: 2; }
  .dot { fill: var(--bg); }
  .spk { stroke: var(--wire); stroke-width: 2.5; stroke-linecap: round; }
  .capside { fill: none; stroke: var(--border-off); stroke-width: 2.5; stroke-linejoin: round; }
  .grille { stroke: var(--off); stroke-width: 2.5; stroke-dasharray: 3 4; }
  .axis { stroke: var(--line); stroke-width: 1; }
  .tick { stroke: var(--dim); stroke-width: 1.5; }
  .body { stroke: var(--label); stroke-width: 12; stroke-linecap: round; }
  .body.slim { stroke-width: 9; }
  .head { stroke: var(--text); stroke-width: 12; stroke-linecap: round; }
  .tip { stroke: var(--accent); stroke-width: 14; stroke-linecap: round; }
  .tail { fill: var(--bg); stroke: var(--accent); stroke-width: 2; cursor: grab; }

  .values { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 18px; margin: 16px 0 14px; }
  .val { display: grid; grid-template-columns: 1fr auto; gap: 2px 10px; }
  .val .n { font: 600 12px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .val b { font: 600 15px var(--f-display); text-align: right; }
  .val em { font: 500 13px var(--f-value); font-style: normal; color: var(--label); margin-left: 4px; }
  .val input { grid-column: 1 / -1; width: 100%; accent-color: var(--accent); cursor: pointer; }

  .curve svg { display: block; width: 100%; height: 150px; border-radius: 10px; background: var(--bg); }
  .grid { stroke: var(--line); stroke-width: 1; vector-effect: non-scaling-stroke; }
  .zero { stroke: var(--wire); stroke-width: 1; vector-effect: non-scaling-stroke; }
  .change { fill: none; stroke: var(--accent); stroke-width: 2.5; vector-effect: non-scaling-stroke; }
  .curve .axis { position: relative; height: 18px; font: 500 11px var(--f-value); color: var(--off); stroke: none; }
  .curve .axis span { position: absolute; top: 3px; transform: translateX(-50%); white-space: nowrap; }
  .curve .axis .db { right: 4px; left: auto; transform: none; top: -146px; }
  .curve .axis .db.low { top: -18px; }
</style>
