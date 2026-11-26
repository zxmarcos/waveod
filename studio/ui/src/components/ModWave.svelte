<!--
  A modulator drawn as the pedal plays it (lib/modwave.ts follows src/engine/modulators.c):
    LFO        `cycles` cycles of its shape (the random ones: the values it takes after the preset loads), where it
               starts, and a point running along it at its rate
    ENVELOPE   two plucked notes (the guitar, grey, on the modulator's -60..0 dB scale) and what the modulator does
               with them (attack, release, sensitivity), a point sweeping the time
    SEQUÊNCIA  a bar per step (drag them: the values), the glides as ramps, the step playing lit
  The point is a preview at the preset's tempo, not read from the pedal. `ends`: the first parameter it moves, its
  values at 0 % and 100 % on the right edge. `mini`: a still thumbnail (no axes, no point).
-->
<script lang="ts">
  import * as W from '../lib/modwave';
  import { DIVISIONS } from '../lib/controls';
  import type { ModSettings } from '../lib/types';

  interface Props {
    m: ModSettings;
    bpm?: number;
    mini?: boolean;
    shape?: string;                   // mini: this shape instead of m's (the shape buttons)
    ends?: { name: string; lo: string; hi: string } | null;
    selStep?: number | null;          // SEQ: the step whose field has the focus
    onstep?: (i: number) => void;     // SEQ: a step was dragged
  }
  let { m, bpm = 120, mini = false, shape, ends = null, selStep = null, onstep }: Props = $props();

  const H = 150, PL = 42, PT = 26, PB = 22;
  let w = $state(640);
  const PR = $derived(ends ? 118 : 12);
  const pw = $derived(Math.max(60, w - PL - PR));
  const ph = H - PT - PB;
  const sh = $derived(shape ?? m.shape);
  const n = $derived(Math.min(Math.max(m.steps, 1), m.step.length));
  const cycles = $derived(W.isRandom(sh) ? 4 : 2);
  const ENV_S = 1.6;
  const levels = W.pluck(ENV_S);
  const env = $derived(m.type === 'env' ? W.envelope(m, levels) : []);
  const per = $derived(W.period(m, bpm));

  // ---------------------------------------------------------------- the running point (a preview)
  let now = $state(0);
  const t0 = performance.now();
  const still = typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  $effect(() => {
    if (mini || still || m.type === 'off') return;
    let raf = 0;
    const frame = (t: number) => { now = (t - t0) / 1000; raf = requestAnimationFrame(frame); };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  });
  const rnd = $derived(W.randomCycles(cycles));
  /** where the point is: x 0..1 across the drawing, value 0..1 */
  const head = $derived.by((): [number, number] => {
    if (m.type === 'lfo') {
      const c = m.phase / 100 + now / per, k = Math.floor(c), p = c - k;
      return [((k % cycles) + p) / cycles, W.lfoAt(sh, p, rnd[k % cycles])];
    }
    if (m.type === 'seq') {
      const c = now / per, k = Math.floor(c), p = c - k;
      return [((k % n) + p) / n, W.seqAt(m, k, p, k === 0)];
    }
    if (m.type === 'env' && env.length) {
      const x = (now % (ENV_S + 0.4)) / ENV_S, b = Math.min(env.length - 1, Math.floor(x * env.length));
      return [Math.min(x, 1), env[b]];
    }
    return [0, 0];
  });

  // ---------------------------------------------------------------- shapes
  const pts = $derived.by((): [number, number][] => {
    if (mini && m.type === 'env') return W.envelope({ ...m, attack: 20, release: 260, sens: 6 }, W.pluck(1)).map((v, i, a) => [i / (a.length - 1), v]);
    if (m.type === 'lfo' || (mini && shape)) return W.lfoPoints(sh, mini ? (W.isRandom(sh) ? 5 : 2) : cycles, mini ? 48 : 240);
    if (m.type === 'seq') return W.seqPoints(m);
    if (m.type === 'env') return env.map((v, i) => [i / (env.length - 1), v]);
    return [];
  });
  const guitar = $derived(m.type === 'env' && !mini
    ? `M${PL} ${PT + ph} ` + levels.map((a, i) => `L${(PL + (i / (levels.length - 1)) * pw).toFixed(1)} ${(PT + (1 - W.levelPos(a)) * ph).toFixed(1)}`).join(' ') + ` L${PL + pw} ${PT + ph} Z`
    : '');
  const X = (x: number) => PL + x * pw;
  const Y = (y: number) => PT + (1 - y) * ph;

  // ---------------------------------------------------------------- dragging the steps
  let svg: SVGSVGElement | undefined = $state();
  let painting = false;
  function stepAt(e: PointerEvent) {
    if (!svg) return;
    const r = svg.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
    const i = Math.floor(((x - PL) / pw) * n);
    if (i < 0 || i >= n) return;
    const v = Math.round(Math.min(1, Math.max(0, (PT + ph - y) / ph)) * 100);
    if (m.step[i] !== v) m.step[i] = v;
    onstep?.(i);
  }
  function down(e: PointerEvent) {
    if (mini || m.type !== 'seq' || e.button !== 0) return;
    painting = true;
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    stepAt(e);
  }
  const move = (e: PointerEvent) => { if (painting) stepAt(e); };
  const up = () => { painting = false; };
  /** the wheel over a bar: its value by 1 (Shift: 10) */
  function wheel(e: WheelEvent) {
    if (mini || m.type !== 'seq' || !svg) return;
    const i = Math.floor(((e.clientX - svg.getBoundingClientRect().left - PL) / pw) * n);
    if (i < 0 || i >= n) return;
    e.preventDefault();
    m.step[i] = Math.min(100, Math.max(0, m.step[i] + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 10 : 1)));
    onstep?.(i);
  }

  const pct = (v: number) => `${Math.round(v * 100)}%`;
</script>

{#if mini}
  <svg class="mini" viewBox="0 0 44 16" aria-hidden="true">
    {#if m.type === 'seq' && !shape}
      {#each m.step.slice(0, n) as v, i (i)}
        <rect x={1 + (i * 42) / n + 0.6} y={15 - (Math.min(v, 100) / 100) * 13} width={Math.max(1, 42 / n - 1.2)}
              height={(Math.min(v, 100) / 100) * 13 + 0.5} rx="0.6" />
      {/each}
    {:else if pts.length}
      <path d={W.path(pts, 42, 13, 1, 1.5)} />
    {/if}
  </svg>
{:else}
  <div class="wave" bind:clientWidth={w}>
    <svg bind:this={svg} width={w} height={H} class:edit={m.type === 'seq'} role={m.type === 'seq' ? 'application' : 'img'}
         aria-label={m.type === 'seq' ? 'Passos da sequência: arraste as barras, ou a roda do mouse sobre uma' : 'Desenho do modulador'}
         onpointerdown={down} onpointermove={move} onpointerup={up} onpointercancel={up} onwheel={wheel}>
      <defs>
        <linearGradient id="mw-fill" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stop-color="var(--accent)" stop-opacity="0.22" />
          <stop offset="1" stop-color="var(--accent)" stop-opacity="0" />
        </linearGradient>
      </defs>

      <!-- the grid: 0 / 50 / 100 %, the cycles or steps -->
      {#each [0, 0.5, 1] as g (g)}
        <line class="grid" class:mid={g === 0.5} x1={PL} x2={PL + pw} y1={Y(g)} y2={Y(g)} />
        {#if g !== 0.5}<text class="ax" x={PL - 8} y={Y(g) + 4} text-anchor="end">{pct(g)}</text>{/if}
      {/each}
      {#if m.type === 'lfo'}
        {#each Array.from({ length: cycles + 1 }, (_, k) => k) as k (k)}
          <line class="grid v" x1={X(k / cycles)} x2={X(k / cycles)} y1={PT} y2={PT + ph} />
        {/each}
        {#each Array.from({ length: cycles }, (_, k) => k) as k (k)}
          <text class="ax" x={X((k + 0.5) / cycles)} y={H - 6} text-anchor="middle">ciclo {k + 1}</text>
        {/each}
      {:else if m.type === 'env'}
        {#each [0, 0.5, 1, 1.5] as s (s)}
          <line class="grid v" x1={X(s / ENV_S)} x2={X(s / ENV_S)} y1={PT} y2={PT + ph} />
          <text class="ax" x={X(s / ENV_S)} y={H - 6} text-anchor="middle">{String(s).replace('.', ',')} s</text>
        {/each}
      {/if}

      {#if m.type === 'off'}
        <text class="empty" x={PL + pw / 2} y={PT + ph / 2 + 5} text-anchor="middle">Desligado: escolha um tipo acima</text>
      {:else if m.type === 'seq'}
        {#each m.step.slice(0, n) as v, i (i)}
          {@const x = X(i / n) + 3}
          {@const bw = pw / n - 6}
          {@const playing = Math.floor(head[0] * n) === i && !still}
          <rect class="slot" class:sel={selStep === i} x={x} y={PT} width={bw} height={ph} rx="4" />
          <rect class="bar" class:playing x={x} y={Y(Math.min(v, 100) / 100)} width={bw} height={Math.max(2, (Math.min(v, 100) / 100) * ph)} rx="4" />
          {#if bw > 22}<text class="val" x={x + bw / 2} y={Math.max(PT + 12, Y(Math.min(v, 100) / 100) - 6)} text-anchor="middle">{v}</text>{/if}
          <text class="ax" class:on={playing} x={x + bw / 2} y={H - 6} text-anchor="middle">{i + 1}</text>
        {/each}
        {#if m.glide}<path class="glide" d={W.path(pts, pw, ph, PL, PT)} />{/if}
      {:else}
        {#if m.type === 'env'}
          <path class="guitar" d={guitar} />
          <text class="legend g" x={PL + pw - 6} y={PT + 12} text-anchor="end">guitarra</text>
          <text class="legend" x={PL + pw - 6} y={PT + 24} text-anchor="end">modulador</text>
        {/if}
        <path class="fill" d={W.path(pts, pw, ph, PL, PT) + ` L${PL + pw} ${PT + ph} L${PL} ${PT + ph} Z`} />
        <path class="line" d={W.path(pts, pw, ph, PL, PT)} />
        {#if m.type === 'lfo' && m.phase}
          {@const x = X(m.phase / 100 / cycles)}
          <path class="start" d="M{x} {PT + ph + 2} l-5 8 h10 z" />
        {/if}
      {/if}

      {#if m.type !== 'off' && !still}
        <line class="head" x1={X(head[0])} x2={X(head[0])} y1={PT} y2={PT + ph} />
        <circle class="dot" cx={X(head[0])} cy={Y(head[1])} r="5.5" />
      {/if}

      <!-- the caption: the rate, the value now -->
      {#if m.type === 'lfo' || m.type === 'seq'}
        <text class="cap" x={PL} y={14}>{W.rateText(m, bpm, DIVISIONS)}</text>
      {:else if m.type === 'env'}
        <text class="cap" x={PL} y={14}>ataque {m.attack} ms · retorno {m.release} ms · sensibilidade {m.sens > 0 ? '+' : ''}{m.sens} dB</text>
      {/if}
      {#if m.type !== 'off' && !still}
        <text class="now" x={PL + pw} y={14} text-anchor="end">{pct(head[1])}</text>
      {/if}

      <!-- what it moves: the first parameter's values at 0 % and 100 % -->
      {#if ends && m.type !== 'off'}
        <line class="end" x1={PL + pw + 6} x2={PL + pw + 6} y1={Y(1)} y2={Y(0)} />
        <text class="pname" x={PL + pw + 14} y={Y(0.5) + 4}>{ends.name}</text>
        <text class="pv" x={PL + pw + 14} y={Y(1) + 4}>{ends.hi}</text>
        <text class="pv" x={PL + pw + 14} y={Y(0) + 4}>{ends.lo}</text>
      {/if}
    </svg>
  </div>
{/if}

<style>
  .wave { width: 100%; min-width: 0; }
  svg { display: block; overflow: visible; }
  svg.edit { cursor: ns-resize; touch-action: none; }
  .grid { stroke: var(--line); stroke-width: 1; }
  .grid.mid { stroke-dasharray: 3 4; }
  .grid.v { stroke: #191816; }
  .ax { font: 500 11px var(--f-value); fill: var(--off); }
  .ax.on { fill: var(--accent); }
  .empty { font: 500 14px var(--f-body); fill: var(--off); }
  .cap { font: 600 12px var(--f-display); letter-spacing: 0.06em; text-transform: uppercase; fill: var(--label); }
  .now { font: 500 13px var(--f-value); fill: var(--text); }
  .line { fill: none; stroke: var(--accent); stroke-width: 2.25; stroke-linejoin: round; stroke-linecap: round; }
  .fill { fill: url(#mw-fill); stroke: none; }
  .guitar { fill: #2a2926; stroke: #4a4844; stroke-width: 1; }
  .legend { font: 600 11px var(--f-display); letter-spacing: 0.06em; text-transform: uppercase; fill: var(--accent); }
  .legend.g { fill: var(--off); }
  .start { fill: var(--label); }
  .head { stroke: var(--text); stroke-opacity: 0.35; stroke-width: 1; }
  .dot { fill: var(--accent); stroke: var(--text); stroke-width: 1.5; }
  .slot { fill: var(--bg); stroke: none; }
  .slot.sel { stroke: var(--text); stroke-width: 1; }
  .bar { fill: var(--wire); transition: fill 80ms; }
  .bar.playing { fill: var(--accent); }
  .val { font: 500 11px var(--f-value); fill: var(--label); pointer-events: none; }
  .glide { fill: none; stroke: var(--text); stroke-opacity: 0.6; stroke-width: 1.25; pointer-events: none; }
  .end { stroke: var(--border-off); stroke-width: 2; }
  .pname { font: 600 11.5px var(--f-display); letter-spacing: 0.05em; text-transform: uppercase; fill: var(--label); }
  .pv { font: 500 12px var(--f-value); fill: var(--text); }
  .mini { width: 44px; height: 16px; flex: none; }
  .mini path { fill: none; stroke: currentColor; stroke-width: 1.4; stroke-linejoin: round; stroke-linecap: round; }
  .mini rect { fill: currentColor; }
</style>
