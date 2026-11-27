<script lang="ts">
  // The chain as the pedal lays it out: 2 rows x 6 cells (src/model/route.h). Series runs row 1 then row 2;
  // parallel splits row 1 at `split` into lane A (up to `merge`) and lane B (the whole row 2); dual L/R sends
  // lane A to the left output and row 2 to the right one.
  import Icon from './Icon.svelte';
  import { menuInfo } from '../lib/menus';
  import { wirePaths } from '../lib/wires';
  import { studio } from '../lib/store.svelte';
  import { t } from '../lib/i18n.svelte';
  import type { Block } from '../lib/types';

  const PITCH = 120, X0 = 112, Y1 = 98, Y2 = 300, BUS = 199, BLOCK = 84;
  const XEND = X0 + 6 * PITCH;
  const W = XEND + 104, H = 388;
  const IN_X = 24, OUT_X = XEND + 28, PILL = 52;

  const rig = $derived(studio.rig);
  const stored = $derived(rig?.route ?? { mode: 0, split: 1, merge: 4, joints: [] });

  // ---------------------------------------------------------------- the joints dragged along row 1
  // The split moves between the columns 0 .. merge - 1 (parallel) or 0 .. 5 (dual L/R), the merge between
  // split + 1 .. 6 (src/model/route.c route_split_range / route_merge_range); the wires follow while dragging and
  // the pedal gets the new place on release (the blocks keep their cells, their lanes follow).
  type Joint = 'split' | 'merge';
  let drag = $state<{ which: Joint; col: number; x0: number; moved: boolean } | null>(null);
  const route = $derived(drag ? { ...stored, [drag.which]: drag.col } : stored);
  function range(which: Joint): [number, number] {
    return which === 'split' ? [0, stored.mode === 2 ? 5 : stored.merge - 1] : [stored.split + 1, 6];
  }
  function colAt(e: PointerEvent) {
    const r = stage!.getBoundingClientRect();
    return Math.round(((e.clientX - r.left) / scale - X0) / PITCH);
  }
  function jointDown(e: PointerEvent, which: Joint) {
    if (e.button !== 0) return;
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    drag = { which, col: stored[which], x0: e.clientX, moved: false };
  }
  function jointMove(e: PointerEvent) {
    if (!drag) return;
    if (Math.abs(e.clientX - drag.x0) > 4) drag.moved = true;
    const [lo, hi] = range(drag.which);
    drag.col = Math.max(lo, Math.min(hi, colAt(e)));
  }
  function jointUp() {
    const d = drag;
    drag = null;
    if (d?.moved) void place(d.which, d.col);
  }
  function place(which: Joint, col: number) {
    const [lo, hi] = range(which);
    const c = Math.max(lo, Math.min(hi, col));
    if (c === stored[which]) return;
    return which === 'split' ? studio.route(stored.mode, c, stored.merge) : studio.route(stored.mode, stored.split, c);
  }
  function jointKey(e: KeyboardEvent, which: Joint) {
    const step = e.key === 'ArrowLeft' ? -1 : e.key === 'ArrowRight' ? 1 : 0;
    if (!step) return;
    e.preventDefault();
    void place(which, stored[which] + step);
  }

  const byCell = $derived(new Map((rig?.blocks ?? []).filter((b) => b.cell >= 0).map((b) => [b.cell, b])));
  const row2Used = $derived([...byCell.keys()].some((c) => c >= 6));
  const xs = $derived(X0 + route.split * PITCH);
  const xm = $derived(X0 + (route.mode === 2 ? 6 : route.merge) * PITCH);

  const cx = (cell: number) => X0 + (cell % 6) * PITCH + PITCH / 2;
  const cy = (cell: number) => (cell < 6 ? Y1 : Y2);

  const GEO = { x0: X0, pitch: PITCH, y1: Y1, y2: Y2, bus: BUS, inEnd: IN_X + PILL, outX: OUT_X, r: 14, pad: 6 };
  const wires = $derived(wirePaths(GEO, route, row2Used));

  // ---------------------------------------------------------------- fit the stage to the space it has
  let box = $state<HTMLDivElement>();
  let stage = $state<HTMLDivElement>();
  let scale = $state(1);
  $effect(() => {
    if (!box) return;
    const ro = new ResizeObserver(([e]) => {
      const { width, height } = e.contentRect;
      scale = Math.max(0.55, Math.min(width / W, height / H, 1.3));
    });
    ro.observe(box);
    return () => ro.disconnect();
  });

  // ---------------------------------------------------------------- selection and drag and drop
  const selSlot = $derived(studio.selection?.kind === 'block' ? studio.selection.slot : -1);
  const selCell = $derived(studio.selection?.kind === 'cell' ? studio.selection.cell : -1);
  const selJoint = $derived(studio.selection?.kind === 'joint' ? studio.selection.which : null);
  let over = $state(-1);

  function dragBlock(e: DragEvent, b: Block) {
    e.dataTransfer?.setData('application/x-waveod-slot', String(b.slot));
    if (e.dataTransfer) e.dataTransfer.effectAllowed = 'move';
  }
  function dragOver(e: DragEvent, cell: number) {
    const types = e.dataTransfer?.types ?? [];
    if (types.includes('application/x-waveod-slot') || types.includes('application/x-waveod-model')) {
      e.preventDefault();
      over = cell;
    }
  }
  function drop(e: DragEvent, cell: number) {
    e.preventDefault();
    over = -1;
    const slot = e.dataTransfer?.getData('application/x-waveod-slot');
    const model = e.dataTransfer?.getData('application/x-waveod-model');
    const here = byCell.get(cell);
    if (slot) void studio.move(Number(slot), cell);
    else if (model) {
      const [menu, index] = model.split(':').map(Number);
      if (here) void studio.replaceAt(here.slot, menu, index);
      else void studio.choose(menu, index, cell);
    }
  }
  const cells = [...Array(12).keys()];
</script>

<div class="fit" bind:this={box}>
  <div class="stage" bind:this={stage} style:width="{W}px" style:height="{H}px" style:transform="scale({scale})">
    <svg class="wires" width={W} height={H} aria-hidden="true">
      {#each wires.main as d (d)}<path {d} />{/each}
    </svg>

    <div class="pill in" style:left="{IN_X}px" style:top="{Y1 - 26}px">IN</div>
    {#each wires.outs as y, i (y)}
      <div class="pill out" style:left="{OUT_X}px" style:top="{y - 26}px">
        {route.mode === 2 ? (i === 0 ? 'L' : 'R') : 'OUT'}
      </div>
    {/each}

    {#each cells as cell (cell)}
      {#if !byCell.has(cell)}
        <button class="empty" class:selected={selCell === cell} class:over={over === cell}
                style:left="{cx(cell) - BLOCK / 2}px" style:top="{cy(cell) - BLOCK / 2}px"
                aria-label={t('grid.empty_aria', { row: cell < 6 ? 1 : 2, col: (cell % 6) + 1 })}
                onclick={() => studio.select({ kind: 'cell', cell })}
                ondragover={(e) => dragOver(e, cell)} ondragleave={() => (over = -1)} ondrop={(e) => drop(e, cell)}>
          <span class="mark"></span>
          <span class="plus"><Icon name="PLUS" size={26} stroke={1.8} /></span>
        </button>
      {/if}
    {/each}

    {#each rig?.blocks.filter((b) => b.cell >= 0) ?? [] as b (b.slot)}
      {@const m = menuInfo(b.menu)}
      <button class="block" class:off={!b.on} class:selected={selSlot === b.slot} class:over={over === b.cell}
              class:missing={b.kind === 'missing'}
              style:--c={m.color} style:--dark={m.dark}
              style:left="{cx(b.cell) - BLOCK / 2}px" style:top="{cy(b.cell) - BLOCK / 2}px"
              draggable="true" ondragstart={(e) => dragBlock(e, b)}
              ondragover={(e) => dragOver(e, b.cell)} ondragleave={() => (over = -1)} ondrop={(e) => drop(e, b.cell)}
              onclick={() => studio.select({ kind: 'block', slot: b.slot })}
              ondblclick={() => studio.toggle(b.slot)}
              aria-label="{m.name}: {b.name}{b.on ? '' : t('grid.off_suffix')}" aria-pressed={selSlot === b.slot}>
        <span class="face"><Icon name={m.icon} size={36} stroke={1.9} /></span>
        <span class="label">{b.name}</span>
      </button>
    {/each}

    {#if route.mode !== 0}
      <button class="joint" class:selected={selJoint === 'split'} class:dragging={drag?.which === 'split'}
              style:left="{xs - 9}px" style:top="{Y1 - 9}px"
              aria-label={t('grid.split')} title={t('grid.split_title')}
              onpointerdown={(e) => jointDown(e, 'split')} onpointermove={jointMove} onpointerup={jointUp}
              onpointercancel={() => (drag = null)} onkeydown={(e) => jointKey(e, 'split')}
              onclick={() => studio.select({ kind: 'joint', which: 'split' })}></button>
      {#if route.mode === 1}
        <button class="joint" class:selected={selJoint === 'merge'} class:dragging={drag?.which === 'merge'}
                style:left="{xm - 9}px" style:top="{Y1 - 9}px"
                aria-label={t('grid.merge')} title={t('grid.merge_title')}
                onpointerdown={(e) => jointDown(e, 'merge')} onpointermove={jointMove} onpointerup={jointUp}
                onpointercancel={() => (drag = null)} onkeydown={(e) => jointKey(e, 'merge')}
                onclick={() => studio.select({ kind: 'joint', which: 'merge' })}></button>
      {/if}
      <span class="lane" style:left="{xs + 12}px" style:top="{Y1 - 66}px">{t('grid.path_a')}</span>
      <span class="lane" style:left="{X0 + 12}px" style:top="{Y2 - 66}px">{t('grid.path_b')}</span>
    {/if}
  </div>
</div>

<style>
  .fit {
    position: relative;
    flex: 1;
    min-height: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    overflow: hidden;
  }
  .stage {
    position: relative;
    flex: none;
    transform-origin: center;
  }
  .wires { position: absolute; inset: 0; pointer-events: none; }
  .wires path {
    fill: none;
    stroke: var(--wire);
    stroke-width: 2;
    transition: d 0.45s var(--ease-out);
  }
  .pill {
    position: absolute;
    width: 52px;
    height: 52px;
    display: grid;
    place-items: center;
    border-radius: 14px;
    background: var(--surface);
    border: 1px solid var(--border-off);
    font: 700 15px/1 var(--f-display);
    letter-spacing: 0.06em;
    color: var(--label);
  }
  .block, .empty {
    position: absolute;
    width: 84px;
    height: 84px;
    transition: left 0.4s var(--ease-out), top 0.4s var(--ease-out);
  }
  /* an empty place: a dot on the wire at rest, a dashed slot with a plus when pointed at, chosen or dropped on */
  .empty {
    display: grid;
    place-items: center;
    border-radius: var(--r-block);
    border: 2px dashed transparent;
    color: var(--label);
    transition: border-color 0.15s, background 0.15s;
  }
  .empty > * { grid-area: 1 / 1; }
  .mark { width: 7px; height: 7px; border-radius: 50%; background: var(--wire); transition: opacity 0.15s; }
  .plus { display: grid; opacity: 0; transition: opacity 0.15s; }
  .empty:hover, .empty.selected, .empty.over { border-color: var(--dim); background: var(--bg); }
  .empty:hover .plus, .empty.selected .plus, .empty.over .plus { opacity: 1; }
  .empty:hover .mark, .empty.selected .mark, .empty.over .mark { opacity: 0; }
  .empty.selected { border-color: var(--accent); color: var(--accent); }
  .empty.over { border-color: var(--text); color: var(--text); }

  .block { display: block; }
  .face {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    border-radius: var(--r-block);
    background: var(--bg);
    border: 2px solid var(--c);
    color: var(--c);
    transition: background 0.2s, border-color 0.2s, color 0.2s, box-shadow 0.2s, transform 0.15s var(--ease-out);
  }
  .block:hover .face { transform: translateY(-2px); box-shadow: 0 6px 14px rgba(0, 0, 0, 0.5); }
  .block:active .face { transform: translateY(0); }
  .block.selected .face {
    background: var(--dark);
    border-width: 3px;
    outline: 2px solid var(--c);
    outline-offset: 3px;
  }
  .block.off .face { border-color: var(--border-off); color: var(--off); }
  .block.off.selected .face { background: var(--surface); outline-color: var(--dim); }
  .block.missing .face { border-style: dashed; border-color: var(--error); color: var(--error); }
  .block.over .face { outline: 2px dashed var(--text); outline-offset: 3px; }
  .label {
    position: absolute;
    top: calc(100% + 10px);
    left: 50%;
    transform: translateX(-50%);
    width: 116px;
    text-align: center;
    font: 600 14px/1.15 var(--f-display);
    color: var(--text);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .block.off .label { color: var(--off); }

  .joint {
    position: absolute;
    width: 18px;
    height: 18px;
    transform: rotate(45deg);
    border-radius: 3px;
    background: var(--bg);
    border: 2px solid var(--accent);
    transition: left 0.4s var(--ease-out), background 0.15s;
    cursor: ew-resize;
    touch-action: none;
  }
  .joint.dragging { transition: background 0.15s; }
  .joint:hover { background: #3a2a10; }
  .joint.selected { background: var(--accent); }
  .lane {
    position: absolute;
    font: 600 11px/1 var(--f-display);
    letter-spacing: 0.1em;
    color: var(--dim);
  }
</style>
