<script lang="ts">
  // A stored preset's chain in miniature: the same 2 x 6 grid and wires as the chain screen, icons only.
  import Icon from './Icon.svelte';
  import { menuInfo } from '../lib/menus';
  import { wirePaths } from '../lib/wires';
  import { t } from '../lib/i18n.svelte';
  import type { PresetDetail } from '../lib/types';

  let { detail, names }: { detail: PresetDetail; names: (b: PresetDetail['blocks'][number]) => string } = $props();

  const PITCH = 52, BLOCK = 40, X0 = 26, Y1 = 30, Y2 = 100, BUS = 65;
  const W = X0 + 6 * PITCH + 26, H = 130;
  const placed = $derived(detail.blocks.filter((b) => b.cell >= 0 && b.cell < 12));
  const row2Used = $derived(placed.some((b) => b.cell >= 6));
  const wires = $derived(wirePaths({ x0: X0, pitch: PITCH, y1: Y1, y2: Y2, bus: BUS, inEnd: 8, outX: W - 8, r: 8, pad: 4 },
                                   detail.route, row2Used));
  const cx = (cell: number) => X0 + (cell % 6) * PITCH + PITCH / 2;
  const cy = (cell: number) => (cell < 6 ? Y1 : Y2);
</script>

<div class="mini" style:width="{W}px" style:height="{H}px" role="img"
     aria-label={t('grid.mini_aria', { blocks: placed.map((b) => names(b)).join(', ') || t('grid.mini_empty') })}>
  <svg width={W} height={H} aria-hidden="true">
    {#each wires.main as d (d)}<path {d} />{/each}
    <circle cx="5" cy={Y1} r="3.5" class="end" />
    {#each wires.outs as y (y)}<circle cx={W - 5} cy={y} r="3.5" class="end" />{/each}
    {#each [...Array(12).keys()].filter((c) => !placed.some((b) => b.cell === c)) as c (c)}
      <circle cx={cx(c)} cy={cy(c)} r="2" class="dot" />
    {/each}
  </svg>
  {#each placed as b (b.slot)}
    {@const m = menuInfo(b.menu)}
    <span class="cell" class:off={!b.on} style:--c={m.color} title="{names(b)}{b.on ? '' : t('grid.off_suffix')}"
          style:left="{cx(b.cell) - BLOCK / 2}px" style:top="{cy(b.cell) - BLOCK / 2}px">
      <Icon name={m.icon} size={20} stroke={2} />
    </span>
  {/each}
</div>

<style>
  .mini { position: relative; flex: none; }
  svg { position: absolute; inset: 0; }
  path { fill: none; stroke: var(--wire); stroke-width: 2; }
  .end { fill: var(--wire); }
  .dot { fill: var(--border-off); }
  .cell {
    position: absolute;
    width: 40px;
    height: 40px;
    display: grid;
    place-items: center;
    border-radius: 11px;
    border: 2px solid var(--c);
    color: var(--c);
    background: var(--bg);
  }
  .cell.off { border-color: var(--border-off); color: var(--off); }
</style>
