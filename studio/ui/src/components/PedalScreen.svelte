<!--
  The pedal itself: which waveod it runs and what its memory holds, how long the sound takes through it (its own
  latency test, the same as GLOBAL › LATÊNCIA), and where its firmware is updated.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import { num as fmtNum, t, tn } from '../lib/i18n.svelte';
  import { pedal, ROUTES } from '../lib/pedal.svelte';
  import { presetLabel, studio } from '../lib/store.svelte';

  $effect(() => { untrack(() => void pedal.enter()); });

  const i = $derived(pedal.info);
  const l = $derived(pedal.latency);
  const num = (v: number, n = 2) => fmtNum(v, n);
  const usedPct = $derived(i && i.store.sectors ? (100 * (i.store.sectors - i.store.free)) / i.store.sectors : 0);
  const good = $derived(!!l && (l.status === 2 || l.status === 3));
  const why = $derived(!l ? '' : l.status === 4 ? t(l.route === 3 ? 'pedal.why_preset' : 'pedal.why_nothing')
                        : l.status === 5 ? t('pedal.why_clip') : '');
  const rest = $derived(l ? l.mean - l.buffers : 0);

  let hover = $state(-1);
  const hex = (v: number) => v.toString(16).toUpperCase().padStart(6, '0');
  const kb = (n: number) => (n >= 1048576 ? `${fmtNum(n / 1048576, 2)} MB` : `${Math.ceil(n / 1024)} KB`);
  const DUMPS = [['all', 'pedal.dump_all'], ['boot', 'pedal.dump_boot']] as const;
</script>

<div class="screen">
  <header class="tools"><h1>{t('tab.pedal')}</h1></header>
  <div class="cards">
    <section class="card about" aria-label={t('pedal.this')}>
      <h2>{t('pedal.this')}</h2>
      {#if i}
        <p class="model"><Icon name="pedal" size={18} stroke={2} /> {i.name}</p>
        <dl>
          <div><dt>{t('pedal.firmware')}</dt><dd>waveod {i.version}</dd></div>
          <div><dt>{t('pedal.protocol')}</dt><dd>{i.protocol}</dd></div>
          <div><dt>{t('pedal.active')}</dt><dd>{presetLabel(studio.rig?.index ?? i.active)}{studio.rig?.dirty ? ` · ${t('pedal.edited')}` : ''}</dd></div>
          <div><dt>{t('tab.presets')}</dt><dd>{t('pedal.preset_counts', { own: i.own, original: i.original, vacant: i.vacant })}</dd></div>
        </dl>
        <h3>{t('pedal.memory')}</h3>
        <div class="meter" role="meter" aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round(usedPct)}
             aria-label={t('pedal.sectors_used')}>
          <span style:width="{Math.max(usedPct, 1)}%"></span>
        </div>
        <p class="small">{t('pedal.store', { kb: Math.round(i.store.live / 1024), free: i.store.free, sectors: i.store.sectors })}</p>
      {:else}
        <p class="small">{t('pedal.reading')}</p>
      {/if}
    </section>

    <section class="card fw" aria-label={t('pedal.firmware')}>
      <h2>{t('pedal.fw_title')}</h2>
      <p class="small">{t('pedal.fw_text')}</p>
      <div class="fwrow">
        <button class="primary" onclick={() => pedal.pickFirmware()}><Icon name="file" size={15} stroke={2} /> {t('pedal.pick_fw')}</button>
        <button onclick={() => studio.show('presets')}><Icon name="save" size={15} stroke={2} /> {t('pedal.go_backup')}</button>
      </div>
    </section>

    <section class="card lat" aria-label={t('pedal.latency')}>
      <h2>{t('pedal.latency')}</h2>
      <p class="small">{t('pedal.lat_text')}</p>
      <div class="row">
        <div class="seg" role="radiogroup" aria-label={t('pedal.route')}>
          {#each ROUTES as r (r.route)}
            <button role="radio" aria-checked={pedal.route === r.route} class:on={pedal.route === r.route}
                    onclick={() => (pedal.route = r.route)}>{r.label}</button>
          {/each}
        </div>
        <button class="primary" onclick={() => pedal.measure()}>{t('pedal.measure')}</button>
      </div>

      {#if l && good}
        <div class="result" class:unstable={l.status === 3}>
          <div class="big"><b>{num(l.ms, 2)}</b> ms</div>
          <div class="what">
            <p>{ROUTES[l.route]?.label} · {t('pedal.lat_line', { samples: num(l.mean), khz: num(l.rate / 1000, 1) })}{l.status === 3 ? ` · ${t('pedal.unstable')}` : ''}</p>
            {#if l.route !== 3}
              <div class="split" aria-hidden="true">
                <span class="buf" style:flex={l.buffers}></span>
                <span class="conv" style:flex={Math.max(rest, 2)}></span>
              </div>
              <p class="small"><i class="key buf"></i>{t('pedal.buffers', { n: l.buffers, ms: num((l.buffers * 1000) / l.rate, 3) })}
                + <i class="key conv"></i>{t('pedal.converters', { n: num(rest, 1), ms: num((rest * 1000) / l.rate, 3) })}</p>
            {:else}
              <p class="small">{t('pedal.preset_adds')}</p>
            {/if}
            <p class="small">{tn('pedal.runs', l.runs, { min: num(l.min), max: num(l.max) })}{l.route !== 3 ? ` · ${l.level_db} dBFS` : ''} ·
              {t('pedal.match', { pct: Math.round(l.match * 100) })}</p>
          </div>
        </div>
      {:else if l}
        <p class="fail">{ROUTES[l.route]?.label}: {l.status_text}{why ? ` · ${why}` : ''}</p>
      {:else}
        <p class="small none">{t('pedal.no_measure')}</p>
      {/if}
    </section>

    <section class="card flash" aria-label={t('pedal.dump_title')}>
      <h2>{t('pedal.dump_title')}</h2>
      <p class="small">{t('pedal.dump_text')}</p>
      <div class="row">
        <div class="seg" role="radiogroup" aria-label={t('pedal.dump_what')}>
          {#each DUMPS as [k, label] (k)}
            <button role="radio" aria-checked={pedal.dumpKind === k} class:on={pedal.dumpKind === k}
                    onclick={() => (pedal.dumpKind = k)}>{t(label)}</button>
          {/each}
        </div>
        <button class="primary" onclick={() => pedal.makeDump()}><Icon name="save" size={15} stroke={2} /> {t('pedal.dump_make')}</button>
        <button class="ghost" onclick={() => pedal.openDump()} title={t('pedal.dump_open_title')}>
          <Icon name="file" size={15} stroke={2} /> {t('pedal.dump_open')}</button>
      </div>

      {#if pedal.dump?.summary}
        {@const areas = pedal.dump.summary}
        <h3>{pedal.dump.file}</h3>
        <div class="map" role="img" aria-label={t('pedal.map_aria')}>
          {#each areas as a, n (a.name)}
            <span class="area" class:hot={hover === n} style:flex={a.end - a.start} title={t('pedal.area_title', { name: a.name, used: kb(a.used), size: kb(a.end - a.start) })}
                  onpointerenter={() => (hover = n)} onpointerleave={() => (hover = -1)} role="presentation">
              <i style:width="{Math.max(a.used ? 2 : 0, (100 * a.used) / (a.end - a.start))}%"></i>
            </span>
          {/each}
        </div>
        <div class="ticks" aria-hidden="true"><span>0</span><span>2 MB</span><span>4 MB</span><span>6 MB</span><span>8 MB</span></div>
        <table class="areas">
          <thead><tr><th>{t('pedal.col_area')}</th><th>{t('pedal.col_range')}</th><th>{t('pedal.col_used')}</th><th>{t('pedal.col_last')}</th></tr></thead>
          <tbody>
            {#each areas as a, n (a.name)}
              <tr class:empty={!a.used} class:hot={hover === n} onpointerenter={() => (hover = n)} onpointerleave={() => (hover = -1)}>
                <td><b>{a.name}</b><small>{a.detail || a.note}</small></td>
                <td class="mono">{hex(a.start)}–{hex(a.end)}</td>
                <td class="mono">{kb(a.used)} <em>{Math.round((100 * a.used) / (a.end - a.start))}%</em></td>
                <td class="mono">{a.last === null ? '—' : hex(a.last)}</td>
              </tr>
            {/each}
          </tbody>
        </table>
      {/if}
    </section>
  </div>
</div>

<style>
  .screen { flex: 1; min-width: 0; display: flex; flex-direction: column; overflow-y: auto; }
  .tools { padding: 16px 24px 14px; border-bottom: 1px solid var(--line); }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  .cards {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 16px;
    padding: 20px 24px 28px;
    max-width: 1280px;
  }
  .card { padding: 18px 20px 20px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); }
  .lat, .flash { grid-column: 1 / -1; }
  .row .ghost {
    display: flex; align-items: center; gap: 7px; height: 36px; padding: 0 14px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); font: 600 15px var(--f-display);
  }
  .row .ghost:hover { background: var(--raised); }
  .flash h3 { text-transform: none; letter-spacing: 0; font: 500 13px var(--f-value); color: var(--text); }
  .map { display: flex; gap: 2px; height: 26px; margin-top: 4px; }
  .area { position: relative; min-width: 6px; border-radius: 3px; background: var(--track); overflow: hidden; cursor: default; }
  .area i { position: absolute; left: 0; top: 0; bottom: 0; background: var(--accent); opacity: 0.75; }
  .area.hot { box-shadow: inset 0 0 0 1px var(--text); }
  .area.hot i { opacity: 1; }
  .ticks { display: flex; justify-content: space-between; margin: 4px 0 14px; font: 500 11px var(--f-value); color: var(--off); }
  .areas { width: 100%; border-collapse: collapse; font-size: 13.5px; }
  .areas th {
    text-align: left; padding: 0 12px 6px 0; font: 600 11.5px var(--f-display); letter-spacing: 0.08em;
    text-transform: uppercase; color: var(--label); border-bottom: 1px solid var(--line);
  }
  .areas td { padding: 7px 12px 7px 0; border-bottom: 1px solid var(--line); vertical-align: top; }
  .areas td b { display: block; font: 600 14.5px var(--f-display); }
  .areas td small { display: block; margin-top: 1px; font-size: 12px; color: var(--label); }
  .areas .mono { font: 500 13px var(--f-value); white-space: nowrap; }
  .areas em { font-style: normal; color: var(--off); margin-left: 4px; }
  .areas tr.empty td b, .areas tr.empty .mono { color: var(--off); }
  .areas tr.hot td { background: var(--raised); }
  h2 { margin: 0 0 12px; font: 700 20px var(--f-display); }
  h3 { margin: 18px 0 8px; font: 600 12.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .model { display: flex; align-items: center; gap: 8px; margin: 0 0 12px; font: 600 16px var(--f-display); color: var(--accent); }
  dl { display: grid; grid-template-columns: auto 1fr; gap: 6px 18px; margin: 0; }
  dl div { display: contents; }
  dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); align-self: center; }
  dd { margin: 0; font: 500 14px var(--f-value); }
  .small { margin: 8px 0 0; font-size: 13px; line-height: 1.5; color: var(--label); }
  .meter { height: 8px; border-radius: 4px; background: var(--track); overflow: hidden; }
  .meter span { display: block; height: 100%; background: var(--accent); }
  .fw button, .row .primary {
    display: flex;
    align-items: center;
    gap: 7px;
    height: 36px;
    padding: 0 14px;
    margin-top: 14px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 15px var(--f-display);
  }
  .fw button:hover { background: var(--raised); }
  .fwrow { display: flex; gap: 8px; flex-wrap: wrap; }
  .fw .primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); font-weight: 700; }
  .fw .primary:hover { background: #f7c169; }
  .row { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; margin-top: 14px; }
  .row .primary { margin: 0; background: var(--accent); color: var(--accent-ink); border-color: var(--accent); font-weight: 700; }
  .row .primary:hover { background: #f7c169; }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--bg); border: 1px solid var(--line); }
  .seg button { height: 30px; padding: 0 12px; border-radius: 7px; font: 600 14.5px var(--f-display); color: var(--label); }
  .seg button:hover:not(.on) { color: var(--text); background: var(--track); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  .result { display: flex; gap: 28px; align-items: flex-start; margin-top: 20px; }
  .big { font: 500 18px var(--f-value); color: var(--label); white-space: nowrap; }
  .big b { font: 700 56px/1 var(--f-display); color: var(--ok); }
  .unstable .big b { color: #f6c15a; }
  .what { flex: 1; min-width: 0; }
  .what > p:first-child { margin: 4px 0 10px; font: 500 14px var(--f-value); }
  .split { display: flex; gap: 3px; height: 10px; max-width: 520px; }
  .split span { border-radius: 3px; }
  .buf { background: var(--ok); }
  .conv { background: var(--dim); }
  .key { display: inline-block; width: 9px; height: 9px; border-radius: 2px; margin-right: 6px; }
  .fail { margin: 18px 0 0; color: #ffb3b0; font-size: 14px; }
  .none { margin-top: 16px; }
  @media (max-width: 1240px) { .cards { grid-template-columns: minmax(0, 1fr); } }
</style>
