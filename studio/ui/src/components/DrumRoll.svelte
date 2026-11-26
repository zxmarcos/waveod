<script lang="ts">
  // What a drum pattern plays, drawn as a drum score reads: the pedal's 13 sounds as rows (cymbals on top, kicks at
  // the bottom), time across in beats and bars, each hit as strong as its velocity.
  import { ROW_ORDER, SOUND_COLOR, SOUNDS } from '../lib/drums.svelte';
  import type { DrumRoll } from '../lib/types';

  let { roll, bar = 480 }: { roll: DrumRoll; bar?: number } = $props();

  const TICKS = 120, ROW = 20, RULER = 22, MIN_BEAT = 26;
  let box = $state<HTMLDivElement>();
  let width = $state(800);
  $effect(() => {
    if (!box) return;
    const ro = new ResizeObserver(([e]) => (width = e.contentRect.width));
    ro.observe(box);
    return () => ro.disconnect();
  });

  const length = $derived(Math.max(roll.length, TICKS));
  const k = $derived(Math.max((width - 2) / length, MIN_BEAT / TICKS));      // px per tick
  const W = $derived(Math.ceil(length * k) + 2);
  const H = RULER + ROW * ROW_ORDER.length;
  const rowOf = new Map(ROW_ORDER.map((s, r) => [s, r]));
  const used = $derived(new Set(roll.hits.map((h) => h[1])));
  const beats = $derived(Math.ceil(length / TICKS));
  const bars = $derived(Math.ceil(length / bar));
  const showBeat = $derived(TICKS * k >= 10);
  const noteW = $derived(Math.max(3, Math.min(14, 30 * k - 2)));
</script>

<div class="roll">
  <div class="names" style:padding-top="{RULER}px">
    {#each ROW_ORDER as s (s)}
      <span class:unused={!used.has(s)} style:--c={SOUND_COLOR(s)} style:height="{ROW}px">{SOUNDS[s]}</span>
    {/each}
  </div>
  <div class="scroll" bind:this={box}>
    <svg width={W} height={H} role="img"
         aria-label="{roll.hits.length} batidas em {bars} {bars === 1 ? 'compasso' : 'compassos'}">
      {#each ROW_ORDER as _, r (r)}
        {#if r % 2 === 0}<rect x="0" y={RULER + r * ROW} width={W} height={ROW} class="band" />{/if}
      {/each}
      {#if showBeat}
        {#each Array(beats) as _, b (b)}
          {#if b % (bar / TICKS) !== 0}<line x1={b * TICKS * k + 1} x2={b * TICKS * k + 1} y1={RULER} y2={H} class="beat" />{/if}
        {/each}
      {/if}
      {#each Array(bars) as _, b (b)}
        <line x1={b * bar * k + 1} x2={b * bar * k + 1} y1="4" y2={H} class="bar" />
        {#if bar * k >= 24 || b % Math.ceil(24 / (bar * k)) === 0}
          <text x={b * bar * k + 6} y="15" class="num">{b + 1}</text>
        {/if}
      {/each}
      <line x1={length * k + 1} x2={length * k + 1} y1="4" y2={H} class="end" />
      {#each roll.hits as [t, s, v], i (i)}
        <rect x={t * k + 1} y={RULER + (rowOf.get(s) ?? 0) * ROW + 4} width={noteW} height={ROW - 8} rx="2"
              fill={SOUND_COLOR(s)} opacity={0.3 + 0.7 * (v / 127)} />
      {/each}
    </svg>
  </div>
</div>

<style>
  .roll {
    display: flex;
    border: 1px solid var(--line);
    border-radius: var(--r-md);
    background: var(--bg);
    overflow: hidden;
  }
  .names {
    flex: none;
    width: 138px;
    display: flex;
    flex-direction: column;
    border-right: 1px solid var(--line);
    background: var(--surface);
  }
  .names span {
    display: flex;
    align-items: center;
    gap: 7px;
    padding: 0 10px;
    font: 600 12.5px var(--f-display);
    color: var(--text);
    white-space: nowrap;
  }
  .names span::before { content: ''; width: 6px; height: 6px; border-radius: 2px; background: var(--c); flex: none; }
  .names span.unused { color: var(--dim); }
  .names span.unused::before { background: var(--border-off); }
  .scroll { flex: 1; min-width: 0; overflow-x: auto; overflow-y: hidden; }
  svg { display: block; }
  .band { fill: rgba(255, 255, 255, 0.018); }
  .beat { stroke: var(--line); stroke-width: 1; }
  .bar { stroke: var(--border-off); stroke-width: 1; }
  .end { stroke: var(--label); stroke-width: 1.5; stroke-dasharray: 3 3; }
  .num { font: 500 10.5px var(--f-value); fill: var(--label); }
</style>
