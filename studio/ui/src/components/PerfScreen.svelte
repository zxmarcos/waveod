<!--
  Desempenho: what the pedal's CPU spends, in cycles of its 64-frame block (protocol 11). On top the preset
  playing, as it plays: one bar for the block (each slot in its menu's colour, the firmware around them, the room
  left) and the slots in chain order. Below, an effect measured on its own (a model of the pedal, or a module file
  read from the disk at every run, never written to the flash): runs kept to compare against a reference, the CRC
  saying whether two runs computed the same output.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import { menuInfo, MENUS, offered } from '../lib/menus';
  import { i18n, num, t, tn } from '../lib/i18n.svelte';
  import { blockUse, perf, sameTarget, sectionName, type Run } from '../lib/perf.svelte';
  import { studio } from '../lib/store.svelte';
  import { CHAIN_SECTION } from '../lib/types';

  $effect(() => {
    untrack(() => perf.enter());
    return () => perf.leave();
  });
  $effect(() => { void studio.ensureCatalog(perf.menu); });

  // ---------------------------------------------------------------- numbers
  const comma = (v: number, n = 1) => num(v, n);
  const kc = (v: number) => (v >= 1e6 ? `${comma(v / 1e6, 2)} M` : v >= 1e4 ? `${comma(v / 1e3, 1)} k` : `${Math.round(v)}`);
  const pct = (v: number, n = 1) => `${comma(100 * v, n)}%`;
  const bytes = (n: number) => (n >= 1024 ? `${comma(n / 1024, 1)} KB` : `${n} B`);
  const time = (s: number) => new Date(s * 1000).toLocaleTimeString(i18n.lang);

  // ---------------------------------------------------------------- the preset playing
  const p = $derived(perf.profile);
  const use = $derived(p ? blockUse(p) : null);
  const share = $derived(use ? use.whole / use.budget : 0);
  const tone = $derived(share > 1 ? 'over' : share > 0.85 ? 'near' : 'ok');
  function blockName(slot: number, menu: number, id: number) {
    const b = studio.rig?.blocks.find((x) => x.slot === slot && x.id === id);
    return b?.name ?? studio.catalogs[menu]?.find((i) => i.id === id)?.name ?? `0x${id.toString(16).toUpperCase()}`;
  }
  const rows = $derived(p ? p.slots.map((s) => ({ ...s, name: blockName(s.slot, s.menu, s.id), info: menuInfo(s.menu) })) : []);
  const top = $derived(rows.reduce((a, r) => Math.max(a, r.max), 1));
  let hot = $state<string | null>(null);     // the hovered segment / row: 's<slot>' or 'fw'
  let tip = $state<{ x: number; text: string } | null>(null);
  let showSections = $state(false);

  function segTip(e: PointerEvent, key: string, text: string) {
    const bar = (e.currentTarget as HTMLElement).parentElement!.getBoundingClientRect();
    hot = key;
    tip = { x: e.clientX - bar.left, text };
  }

  // the history: the whole block's share over the last readings
  const spark = $derived.by(() => {
    const h = perf.history;
    if (h.length < 2) return '';
    const hi = Math.max(1, ...h);
    return h.map((v, k) => `${(k / (h.length - 1)) * 160},${30 - (v / hi) * 28}`).join(' ');
  });

  // ---------------------------------------------------------------- the bench
  const menus = MENUS.filter((m) => m.menu <= 15);
  const items = $derived((studio.catalogs[perf.menu] ?? []).filter((i) => i.kind !== 'missing' && i.kind !== 'routing' && offered(perf.menu, i)));
  $effect(() => {
    const list = items;
    if (list.length && !list.some((i) => i.index === perf.index)) untrack(() => (perf.index = list[0].index));
  });
  const ref = $derived(perf.refRun);
  const shown = $derived(perf.runs.find((r) => r.id === perf.shown) ?? null);
  const BLOCKS = [200, 1000, 2000];

  function delta(r: Run) {
    if (!ref || ref.id === r.id || r.result.emu) return '';
    const d = Math.round(1000 * (r.result.median / ref.result.median - 1)) / 10;
    return d === 0 ? `${comma(0, 1)}%` : `${d > 0 ? '+' : ''}${comma(d, 1)}%`;
  }
  function crcMark(r: Run) {
    if (!ref || ref.id === r.id) return 'ref';
    if (r.result.bypass !== ref.result.bypass) return '';
    return r.result.crc === ref.result.crc ? 'same' : 'diff';
  }

  // the chart of the shown run's blocks
  let chartW = $state(800);
  const CH = 150, PAD = { l: 52, r: 112, t: 10, b: 20 };
  const chart = $derived.by(() => {
    const r = shown?.result;
    if (!r || !r.cycles.length) return null;
    const c = r.cycles;
    const lo = Math.min(...c) * 0.97, hi = Math.max(...c) * 1.02;
    const w = Math.max(200, chartW) - PAD.l - PAD.r, h = CH - PAD.t - PAD.b;
    const x = (k: number) => PAD.l + (c.length > 1 ? (k / (c.length - 1)) * w : w / 2);
    const y = (v: number) => PAD.t + h - ((v - lo) / Math.max(1, hi - lo)) * h;
    return { c, x, y, lo, hi, w, h, line: c.map((v, k) => `${x(k).toFixed(1)},${y(v).toFixed(1)}`).join(' '), r };
  });
  let cross = $state<number | null>(null);
  function chartMove(e: PointerEvent) {
    if (!chart) return;
    const r = (e.currentTarget as SVGElement).getBoundingClientRect();
    const k = Math.round(((e.clientX - r.left - PAD.l) / chart.w) * (chart.c.length - 1));
    cross = k >= 0 && k < chart.c.length ? k : null;
  }
</script>

<div class="screen">
  <header class="tools">
    <h1>{t('tab.perf')}</h1>
    <p>{t('perf.sub')}</p>
  </header>

  {#if !perf.supported}
    <div class="cards"><section class="card wide"><p class="note">{t('perf.unsupported', { protocol: studio.hello?.protocol ?? '?' })}</p></section></div>
  {:else}
  <div class="cards">
    {#if perf.emu}
      <p class="emu wide"><b>{t('perf.emu_label')}</b> {t('perf.emu_text')}</p>
    {/if}

    <!-- ============================================================ the preset playing -->
    <section class="card wide" aria-label={t('perf.playing')}>
      <div class="head">
        <h2>{t('perf.playing')}</h2>
        <div class="liveset">
          <button class="toggle" class:on={perf.live} role="switch" aria-checked={perf.live} onclick={() => perf.setLive(!perf.live)}
                  title={t('perf.live_title')}>
            <span class="dot"></span>{perf.live ? t('perf.live') : t('perf.stopped')}
          </button>
          {#if !perf.live}
            <button class="ghost" disabled={perf.reading} onclick={() => perf.readOnce()}>{perf.reading ? t('perf.reading') : t('perf.read_now')}</button>
          {/if}
        </div>
      </div>

      {#if p && use}
        <div class="hero">
          <div class="big {tone}"><b>{pct(share)}</b><span>{t('perf.of_block')}</span></div>
          <dl class="facts">
            <div><dt>{t('perf.effects')}</dt><dd>{kc(use.slots)} <em>{pct(use.slots / use.budget)}</em></dd></div>
            <div><dt>{t('perf.firmware')}</dt><dd>{kc(use.firmware)} <em>{pct(use.firmware / use.budget)}</em></dd></div>
            <div><dt>{t('perf.room')}</dt><dd>{kc(Math.max(0, use.budget - use.whole))} <em>{pct(Math.max(0, 1 - share))}</em></dd></div>
            <div><dt>{t('perf.worst')}</dt><dd>{comma(p.worst_ms, 3)} ms <em>{pct(p.worst_ms / 1.4512)}</em></dd></div>
          </dl>
          {#if spark}
            <figure class="spark" aria-label={t('perf.spark_aria')}>
              <svg width="160" height="32" viewBox="0 0 160 32" aria-hidden="true"><polyline points={spark} /></svg>
              <figcaption>{tn('perf.last_readings', perf.history.length)}</figcaption>
            </figure>
          {/if}
        </div>

        <div class="budget" role="img" aria-label={t('perf.budget_aria', { effects: pct(use.slots / use.budget), firmware: pct(use.firmware / use.budget) })}
             onpointerleave={() => { hot = null; tip = null; }}>
          <div class="fill" style:width="{Math.min(100, 100 * share)}%">
            {#each rows as r (r.slot)}
              <span class="part" class:hot={hot === `s${r.slot}`} style:flex={Math.max(r.mean, 1)} style:background={r.info.color}
                    role="presentation"
                    onpointermove={(e) => segTip(e, `s${r.slot}`, `${r.info.tag} ${r.name} · ${kc(r.mean)} · ${pct(r.mean / use.budget)}`)}></span>
            {/each}
            <span class="part fw" class:hot={hot === 'fw'} style:flex={Math.max(use.firmware, 1)} role="presentation"
                  onpointermove={(e) => segTip(e, 'fw', `${t('perf.firmware')} · ${kc(use.firmware)} · ${pct(use.firmware / use.budget)}`)}></span>
          </div>
          {#if use.worst > 0}<i class="worst" style:left="{Math.min(100, (100 * use.worst) / use.budget)}%" title={t('perf.worst')}></i>{/if}
          {#if tip}<span class="tip" style:left="{tip.x}px">{tip.text}</span>{/if}
        </div>
        <div class="scale" aria-hidden="true"><span>0</span><span>25%</span><span>50%</span><span>75%</span><span>{t('perf.cycles', { n: kc(use.budget) })}</span></div>

        <table class="slots">
          <thead><tr><th>{t('perf.col_block')}</th><th class="n">{t('perf.col_mean')}</th><th class="n">{t('perf.col_max')}</th><th class="n">{t('perf.col_last')}</th><th class="n">{t('perf.col_use')}</th><th class="barcol"></th></tr></thead>
          <tbody>
            {#each rows as r (r.slot)}
              <tr class:hot={hot === `s${r.slot}`} onpointerenter={() => (hot = `s${r.slot}`)} onpointerleave={() => (hot = null)}>
                <td><i class="chip" style:background={r.info.color}></i><span class="tag">{r.info.tag}</span> {r.name}</td>
                <td class="n">{kc(r.mean)}</td>
                <td class="n">{kc(r.max)}</td>
                <td class="n">{kc(r.last)}</td>
                <td class="n">{pct(r.mean / use.budget)}</td>
                <td class="barcol"><span class="mini"><span style:width="{(100 * r.mean) / top}%" style:background={r.info.color}></span><i style:left="{(100 * r.max) / top}%"></i></span></td>
              </tr>
            {:else}
              <tr><td colspan="6" class="none">{t('perf.no_blocks')}</td></tr>
            {/each}
            <tr class="fwrow" class:hot={hot === 'fw'}>
              <td><i class="chip fw"></i><button class="link" onclick={() => (showSections = !showSections)} aria-expanded={showSections}>
                {t('perf.firmware')} <Icon name={showSections ? 'down' : 'next'} size={13} stroke={2.2} /></button></td>
              <td class="n">{kc(use.firmware)}</td><td class="n"></td><td class="n"></td>
              <td class="n">{pct(use.firmware / use.budget)}</td><td class="barcol"></td>
            </tr>
            {#if showSections}
              {#each p.sections as c, k (k)}
                {#if k !== CHAIN_SECTION && c > 0}
                  <tr class="sec"><td>{sectionName(k)}</td><td class="n">{kc(c)}</td><td></td><td></td><td class="n">{pct(c / use.budget)}</td><td></td></tr>
                {/if}
              {/each}
              <tr class="sec"><td>{t('perf.between')}</td><td class="n">{kc(use.between)}</td><td></td><td></td><td class="n">{pct(use.between / use.budget)}</td><td></td></tr>
            {/if}
          </tbody>
        </table>
        <p class="small">{tn('perf.profile_note', p.blocks, { mhz: Math.round(p.core_hz / 1e6) })}</p>
      {:else}
        <p class="note">{t('pedal.reading')}</p>
      {/if}
    </section>

    <!-- ============================================================ one effect on its own -->
    <section class="card wide" aria-label={t('perf.bench')}>
      <h2>{t('perf.bench')}</h2>
      <!-- the catalog's own text, with <b> around the button's name -->
      <p class="small">{@html t('perf.bench_text')}</p>

      <div class="pick">
        <div class="seg" role="radiogroup" aria-label={t('perf.what')}>
          <button role="radio" aria-checked={perf.source === 'catalog'} class:on={perf.source === 'catalog'} onclick={() => (perf.source = 'catalog')}>{t('perf.from_pedal')}</button>
          <button role="radio" aria-checked={perf.source === 'file'} class:on={perf.source === 'file'} onclick={() => (perf.source = 'file')}>{t('perf.from_file')}</button>
        </div>
        {#if perf.source === 'catalog'}
          <select bind:value={perf.menu} aria-label={t('perf.menu')}>
            {#each menus as m (m.menu)}<option value={m.menu}>{m.name}</option>{/each}
          </select>
          <select bind:value={perf.index} aria-label={t('perf.model')} disabled={!items.length}>
            {#each items as it (it.index)}<option value={it.index}>{it.name}{it.kind === 'library' ? '' : ` · ${it.kind}`}</option>{/each}
          </select>
        {:else}
          <button class="ghost" onclick={() => perf.pickModule()}><Icon name="file" size={15} stroke={2} /> {t('perf.pick_file')}</button>
          {#if perf.module}
            {@const m = perf.module}
            <span class="file">
              {#if m.name}<b>{m.name}</b>{/if}
              {m.file}{#if m.source}<span class="flag">{t('perf.source_flag')}</span>{/if}
              {#if m.size} · {bytes(m.size)}{/if}{#if m.menu !== undefined} · {menuInfo(m.menu).name}{/if} · {t('perf.saved_at', { time: time(m.modified) })}
            </span>
            <button class="toggle" class:on={perf.watching} role="switch" aria-checked={perf.watching}
                    onclick={() => perf.setWatch(!perf.watching)}
                    title={t(m.source ? 'perf.watch_title_src' : 'perf.watch_title')}>
              <span class="dot"></span>{perf.watching ? t('perf.watching') : t('perf.watch')}
            </button>
          {/if}
        {/if}
      </div>
      {#if perf.source === 'file' && perf.build}
        <div class="buildlog" role="alert">
          <p><b>{perf.build.error}</b></p>
          {#if perf.build.log}<pre>{perf.build.log}</pre>{/if}
        </div>
      {/if}
      <div class="pick">
        <div class="seg" role="radiogroup" aria-label={t('perf.blocks_aria')}>
          {#each perf.emu ? [20, 50, 200] : BLOCKS as n (n)}
            <button role="radio" aria-checked={perf.opts.blocks === n} class:on={perf.opts.blocks === n}
                    onclick={() => (perf.opts.blocks = n)}>{tn('perf.n_blocks', n)}</button>
          {/each}
        </div>
        <label class="check" title={t('perf.cold_title')}>
          <input type="checkbox" bind:checked={perf.opts.cold} /> {t('perf.cold')}</label>
        <label class="check" title={t('perf.bypass_title')}>
          <input type="checkbox" bind:checked={perf.opts.bypass} /> {t('perf.bypass')}</label>
        <button class="primary" disabled={!perf.ready} onclick={() => perf.measure()}>{t('perf.measure')}</button>
      </div>

      {#if perf.runs.length}
        <table class="runs">
          <thead><tr>
            <th>{t('perf.col_run')}</th><th class="n">{t('perf.col_median')}</th><th class="n">p99</th><th class="n">{t('perf.col_max')}</th><th class="n">{t('perf.col_use')}</th>
            <th class="n" title={t('perf.delta_title')}>Δ</th><th>CRC</th><th class="n">{t('perf.col_load')}</th><th class="n">{t('perf.col_memory')}</th><th></th>
          </tr></thead>
          <tbody>
            {#each perf.runs as r (r.id)}
              {@const m = crcMark(r)}
              {@const d = delta(r)}
              <tr class:shown={perf.shown === r.id} onclick={() => (perf.shown = r.id)}>
                <td>
                  <i class="chip" style:background={menuInfo(r.result.menu).color}></i>
                  <b>{r.label}</b>{#if r.result.cold}<span class="flag">{t('perf.flag_cold')}</span>{/if}{#if r.result.bypass}<span class="flag">{t('perf.flag_bypass')}</span>{/if}
                  <small>{r.detail} · {tn('perf.n_blocks', r.result.blocks)}{#if r.result.build_s} · {t('perf.built_in', { s: comma(r.result.build_s, 1) })}{/if} · {new Date(r.at).toLocaleTimeString(i18n.lang)}</small>
                </td>
                <td class="n">{kc(r.result.median)}</td>
                <td class="n">{kc(r.result.p99)}</td>
                <td class="n">{kc(r.result.max)}</td>
                <td class="n">{r.result.emu ? '—' : pct(r.result.median / r.result.budget)}</td>
                <td class="n delta" class:better={d.startsWith('-')} class:worse={d.startsWith('+')}>{d}</td>
                <td class="crc">
                  <code>{r.result.crc}</code>
                  {#if m === 'ref'}<span class="mark ref">{t('perf.ref')}</span>
                  {:else if m === 'same'}<span class="mark same">{t('perf.same')}</span>
                  {:else if m === 'diff'}<span class="mark diff" title={t(sameTarget(r, ref!) ? 'perf.diff_out' : 'perf.diff_other')}>{t('perf.diff')}</span>{/if}
                </td>
                <td class="n">{kc(r.result.load)}</td>
                <td class="n" title={t('perf.pools', { code: bytes(r.result.pools.code), const: bytes(r.result.pools.const), state: bytes(r.result.pools.state), buffer: bytes(r.result.pools.buffer) })}>
                  {bytes(r.result.pools.code + r.result.pools.const + r.result.pools.state + r.result.pools.buffer)}</td>
                <td class="acts">
                  <button title={t(r.target.kind === 'file' ? 'perf.again_file' : 'perf.again')} aria-label={t('perf.again')}
                          onclick={(e) => { e.stopPropagation(); void perf.measure(r); }}><Icon name="loop" size={14} stroke={2} /></button>
                  {#if perf.ref !== r.id}
                    <button title={t('perf.use_ref')} onclick={(e) => { e.stopPropagation(); perf.ref = r.id; }}>ref</button>
                  {/if}
                  <button title={t('perf.remove')} aria-label={t('perf.remove')} onclick={(e) => { e.stopPropagation(); perf.remove(r.id); }}><Icon name="close" size={14} stroke={2} /></button>
                </td>
              </tr>
            {/each}
          </tbody>
        </table>

        {#if chart && shown}
          <figure class="chart" bind:clientWidth={chartW}>
            <figcaption><b>{shown.label}</b> · {t('perf.chart_caption')}{shown.result.emu ? ` (${t('perf.emu_no_value')})` : ''}</figcaption>
            <svg width={Math.max(200, chartW)} height={CH} role="img" aria-label={t('perf.chart_aria', { name: shown.label })}
                 onpointermove={chartMove} onpointerleave={() => (cross = null)}>
              {#each [chart.r.median, chart.r.p99] as v, k (k)}
                <line class="rule" x1={PAD.l} x2={PAD.l + chart.w} y1={chart.y(v)} y2={chart.y(v)} />
                <text class="rlabel" x={PAD.l + chart.w + 6} y={chart.y(v) + (k ? -2 : 10)}>{k ? 'p99' : t('perf.median')} {kc(v)}</text>
              {/each}
              <text class="axis" x={PAD.l - 6} y={PAD.t + 8} text-anchor="end">{kc(chart.hi)}</text>
              <text class="axis" x={PAD.l - 6} y={PAD.t + chart.h} text-anchor="end">{kc(chart.lo)}</text>
              <text class="axis" x={PAD.l} y={CH - 4}>{t('perf.block_n', { n: 1 })}</text>
              <text class="axis" x={PAD.l + chart.w} y={CH - 4} text-anchor="end">{chart.c.length}</text>
              <polyline class="series" points={chart.line} />
              {#if cross !== null}
                <line class="cross" x1={chart.x(cross)} x2={chart.x(cross)} y1={PAD.t} y2={PAD.t + chart.h} />
                <circle class="dotm" cx={chart.x(cross)} cy={chart.y(chart.c[cross])} r="4" />
              {/if}
            </svg>
            {#if cross !== null}
              <span class="ctip" style:left="{Math.min(chart.x(cross), Math.max(200, chartW) - 150)}px">{t('perf.block_n', { n: cross + 1 })} · {t('perf.cycles', { n: chart.c[cross].toLocaleString(i18n.lang) })}</span>
            {/if}
          </figure>
        {/if}
        <div class="foot">
          <p class="small">{t('perf.foot')}</p>
          <button class="ghost" onclick={() => perf.clear()}>{t('perf.clear')}</button>
        </div>
      {/if}
    </section>
  </div>
  {/if}
</div>

<style>
  .screen { flex: 1; min-width: 0; display: flex; flex-direction: column; overflow-y: auto; }
  .tools { display: flex; align-items: baseline; gap: 18px; padding: 16px 24px 14px; border-bottom: 1px solid var(--line); }
  .tools p { margin: 0; font-size: 13px; color: var(--label); }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  h2 { margin: 0; font: 700 20px var(--f-display); }
  .cards { display: grid; grid-template-columns: minmax(0, 1fr); gap: 16px; padding: 20px 24px 28px; max-width: 1280px; }
  .card { padding: 18px 20px 20px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); }
  .emu { margin: 0; padding: 10px 14px; border-radius: var(--r-sm); border: 1px solid var(--border-off); font-size: 13px; color: var(--label); }
  .emu b { color: var(--accent); }
  .note { margin: 14px 0 0; color: var(--label); font-size: 14px; }
  .small { margin: 10px 0 0; font-size: 12.5px; line-height: 1.5; color: var(--label); }
  .head { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
  .liveset { display: flex; gap: 8px; }
  .toggle, .ghost, .primary {
    display: flex; align-items: center; gap: 7px; height: 32px; padding: 0 12px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); font: 600 14px var(--f-display); color: var(--label);
  }
  .toggle .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--dim); }
  .toggle.on { color: var(--text); }
  .toggle.on .dot { background: var(--ok); animation: blink 1.6s ease-in-out infinite; }
  @keyframes blink { 50% { opacity: 0.35; } }
  @media (prefers-reduced-motion: reduce) { .toggle.on .dot { animation: none; } }
  .ghost:hover:not(:disabled), .toggle:hover { background: var(--raised); color: var(--text); }
  .ghost:disabled { opacity: 0.5; cursor: default; }
  .primary { height: 36px; padding: 0 18px; background: var(--accent); color: var(--accent-ink); border-color: var(--accent); font-weight: 700; font-size: 15px; }
  .primary:hover:not(:disabled) { background: #f7c169; }
  .primary:disabled { background: var(--surface); color: var(--dim); border-color: var(--line); cursor: default; }

  .hero { display: flex; align-items: center; gap: 36px; margin: 16px 0 14px; flex-wrap: wrap; }
  .big { display: flex; align-items: baseline; gap: 8px; }
  .big b { font: 700 52px/1 var(--f-display); color: var(--text); }
  .big span { font: 500 14px var(--f-value); color: var(--label); }
  .big.near b { color: #f6c15a; }
  .big.over b { color: var(--error); }
  .facts { display: grid; grid-template-columns: repeat(4, auto); gap: 4px 28px; margin: 0; }
  .facts div { display: flex; flex-direction: column; gap: 3px; }
  .facts dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .facts dd { margin: 0; font: 500 15px var(--f-value); }
  .facts em { font-style: normal; color: var(--off); margin-left: 4px; font-size: 13px; }
  .spark { margin: 0 0 0 auto; display: flex; flex-direction: column; align-items: flex-end; gap: 2px; }
  .spark polyline { fill: none; stroke: var(--label); stroke-width: 1.5; stroke-linejoin: round; }
  .spark figcaption { font: 500 11px var(--f-value); color: var(--off); }

  .budget { position: relative; height: 30px; border-radius: 6px; background: var(--track); }
  .fill { display: flex; gap: 2px; height: 100%; min-width: 4px; }
  .part { min-width: 3px; height: 100%; opacity: 0.9; }
  .part:first-child { border-radius: 6px 0 0 6px; }
  .part:last-child { border-radius: 0 4px 4px 0; }
  .part.fw { background: var(--dim); }
  .part.hot { opacity: 1; box-shadow: inset 0 0 0 2px var(--text); }
  .worst { position: absolute; top: -4px; bottom: -4px; width: 2px; margin-left: -1px; background: var(--text); border-radius: 1px; }
  .tip {
    position: absolute; bottom: calc(100% + 8px); transform: translateX(-50%); white-space: nowrap; pointer-events: none;
    padding: 5px 9px; border-radius: var(--r-sm); background: var(--raised); border: 1px solid var(--border-off);
    font: 500 12.5px var(--f-value); box-shadow: var(--float-shadow); z-index: 2;
  }
  .scale { display: flex; justify-content: space-between; margin: 5px 0 14px; font: 500 11px var(--f-value); color: var(--off); }

  table { width: 100%; border-collapse: collapse; font-size: 14px; }
  th { text-align: left; padding: 0 12px 6px 0; font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); border-bottom: 1px solid var(--line); white-space: nowrap; }
  td { padding: 7px 12px 7px 0; border-bottom: 1px solid var(--line); vertical-align: middle; }
  .n { text-align: right; font: 500 13.5px var(--f-value); white-space: nowrap; }
  th.n { font: 600 11.5px var(--f-display); }
  .chip { display: inline-block; width: 10px; height: 10px; border-radius: 3px; margin-right: 8px; vertical-align: -1px; }
  .chip.fw { background: var(--dim); }
  .tag { font: 600 11.5px var(--f-display); letter-spacing: 0.06em; color: var(--label); margin-right: 4px; }
  .slots tbody tr.hot td { background: var(--raised); }
  .barcol { width: 26%; }
  .mini { position: relative; display: block; height: 8px; border-radius: 4px; background: var(--track); }
  .mini span { display: block; height: 100%; border-radius: 4px; opacity: 0.9; }
  .mini i { position: absolute; top: -2px; bottom: -2px; width: 2px; margin-left: -1px; background: var(--label); border-radius: 1px; }
  .fwrow td { color: var(--text); }
  .link { display: inline-flex; align-items: center; gap: 4px; font: inherit; color: inherit; }
  .link:hover { color: var(--accent); }
  .sec td { padding: 4px 12px 4px 18px; font-size: 13px; color: var(--label); border-bottom-color: transparent; }
  .sec .n { color: var(--label); }
  .none { color: var(--label); }

  .pick { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; margin-top: 14px; }
  .pick .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--bg); border: 1px solid var(--line); }
  .pick .seg button { height: 28px; padding: 0 12px; border-radius: 7px; font: 600 14px var(--f-display); color: var(--label); }
  .pick .seg button:hover:not(.on) { color: var(--text); background: var(--track); }
  .pick .seg button.on { background: var(--accent); color: var(--accent-ink); }
  select { height: 34px; padding: 0 10px; border-radius: var(--r-sm); background: var(--bg); border: 1px solid var(--border-off); color: var(--text); font: 500 14px var(--f-body); min-width: 160px; }
  select:focus { outline: none; border-color: var(--accent); }
  .file { font-size: 13px; color: var(--label); }
  .buildlog { margin-top: 12px; padding: 10px 14px; border-radius: var(--r-sm); border: 1px solid #6b2f2c; background: #1d1211; }
  .buildlog p { margin: 0; font-size: 13.5px; color: #ffb3b0; }
  .buildlog pre {
    margin: 8px 0 0; max-height: 260px; overflow: auto; font: 500 12.5px/1.45 var(--f-value); color: var(--text);
    white-space: pre-wrap; user-select: text;
  }
  .file b { color: var(--text); font: 600 15px var(--f-display); margin-right: 6px; }
  .check { display: flex; align-items: center; gap: 6px; font-size: 14px; color: var(--label); cursor: pointer; }
  .check input { accent-color: var(--accent); }
  .pick .primary { margin-left: auto; }

  .runs { margin-top: 18px; }
  .runs tbody tr { cursor: pointer; }
  .runs tbody tr:hover td { background: var(--raised); }
  .runs tbody tr.shown td { background: var(--bg); }
  .runs td b { font: 600 15px var(--f-display); }
  .runs td small { display: block; margin: 2px 0 0 18px; font-size: 12px; color: var(--label); }
  .flag { margin-left: 6px; padding: 1px 6px; border-radius: 4px; border: 1px solid var(--border-off); font: 600 11px var(--f-display); color: var(--label); }
  .delta.better { color: var(--ok); }
  .delta.worse { color: #ffb3b0; }
  .crc { white-space: nowrap; }
  .crc code { font: 500 12.5px var(--f-value); color: var(--label); }
  .mark { display: inline-flex; align-items: center; gap: 3px; margin-left: 6px; font: 600 11.5px var(--f-display); letter-spacing: 0.04em; }
  .mark.ref { color: var(--accent); }
  .mark.same { color: var(--ok); }
  .mark.diff { color: #ffb3b0; }
  .acts { white-space: nowrap; text-align: right; }
  .acts button { height: 26px; min-width: 26px; padding: 0 6px; margin-left: 2px; border-radius: var(--r-sm); color: var(--label); font: 600 12px var(--f-display); display: inline-flex; align-items: center; justify-content: center; }
  .acts button:hover { background: var(--track); color: var(--text); }

  .chart { position: relative; margin: 18px 0 0; }
  .chart figcaption { margin-bottom: 6px; font-size: 13px; color: var(--label); }
  .chart figcaption b { color: var(--text); font: 600 14px var(--f-display); }
  .chart svg { display: block; }
  .series { fill: none; stroke: var(--accent); stroke-width: 1.5; stroke-linejoin: round; }
  .rule { stroke: var(--border-off); stroke-width: 1; stroke-dasharray: 4 4; }
  .rlabel, .axis { font: 500 11px var(--f-value); fill: var(--label); }
  .cross { stroke: var(--label); stroke-width: 1; }
  .dotm { fill: var(--accent); stroke: var(--surface); stroke-width: 2; }
  .ctip {
    position: absolute; top: 24px; margin-left: 10px; white-space: nowrap; pointer-events: none; padding: 4px 8px;
    border-radius: var(--r-sm); background: var(--raised); border: 1px solid var(--border-off); font: 500 12px var(--f-value);
  }
  .foot { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-top: 10px; }
  .foot .small { margin: 0; }
</style>
