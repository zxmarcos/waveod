<!--
  THESIS: the pedal's GLOBAL screen, one section at a time: the sections in a sidebar (each with what it is set to
  now), the chosen one alone on the right with room for its settings. The global EQ is a section of its own, drawn
  like the pedal draws it. Each change is heard at once; the pedal stores it 2 s after the last one.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import EqPanel from './EqPanel.svelte';
  import { CARDS, type Card, type Row } from '../lib/globalset';
  import type { IconName } from '../lib/menus';
  import { globals } from '../lib/globals.svelte';
  import { studio } from '../lib/store.svelte';

  $effect(() => {
    untrack(() => {
      // the demonstration's ?section= (design reviews, screenshots)
      const sec = studio.link?.transport === 'demonstração' ? new URLSearchParams(location.search).get('section') : null;
      if (sec) globals.section = sec;
      void globals.enter();
    });
  });

  /** what each section is for, under its title */
  const ABOUT: Record<string, string> = {
    eq: 'O equalizador depois da cadeia, em todos os presets.',
    io: 'O tipo e o nível da entrada, a entrada AUX e em quais saídas tocam os blocos de gabinete.',
    tuner: 'A referência do afinador, o som enquanto se afina e o tipo de visor.',
    usb: 'Os canais de áudio USB, os níveis de gravação e de retorno e o que vai para cada canal gravado.',
    jack: 'O que se liga no jack EXP 2 e a função de cada footswitch externo.',
    midi: 'De onde o pedal recebe MIDI, em que canais, e o clock de tempo.',
    presets: 'O que vale para todos os presets: tempo global, rastro na troca, knobs nas cenas e o motor de gabinete.',
    screen: 'O brilho da tela e a marca de toque.',
    exp: 'A faixa calibrada de cada pedal de expressão.',
    system: 'A temperatura do processador do pedal, como em GLOBAL › SISTEMA.',
  };

  // SISTEMA: read every two seconds while open
  $effect(() => {
    if (globals.section === 'system') untrack(() => void globals.watchSystem());
    else untrack(() => globals.stopSystem());
    return () => globals.stopSystem();
  });
  const TEMP_LO = 20, TEMP_HI = 95;           // the bar: room to the part's limit (consumer grade, 95 °C)
  const deg = (t: number | null, n = 1) => (t === null ? '—' : `${t.toFixed(n).replace('.', ',')} °C`);
  const at = (t: number) => Math.min(100, Math.max(0, ((t - TEMP_LO) / (TEMP_HI - TEMP_LO)) * 100));
  const now = $derived(globals.temp('cpu_temp'));
  const tmax = $derived(globals.temp('cpu_temp_max'));
  const tmin = $derived(globals.temp('cpu_temp_min'));
  const tone = $derived(now === null ? '' : now >= 85 ? 'hot' : now >= 70 ? 'warm' : 'ok');
  // the history since SISTEMA opened: a line on a fixed 20 .. 95 °C scale would flatten it; its own range, padded
  let histW = $state(600);
  const HH = 120, HP = { l: 46, r: 12, t: 10, b: 20 };
  const hist = $derived.by(() => {
    const h = globals.history;
    if (h.length < 2) return null;
    const vs = h.map((p) => p.v);
    const lo = Math.floor(Math.min(...vs) - 1), hi = Math.ceil(Math.max(...vs) + 1);
    const t0 = h[0].t, t1 = h[h.length - 1].t, w = Math.max(100, histW) - HP.l - HP.r, hh = HH - HP.t - HP.b;
    const x = (t: number) => HP.l + ((t - t0) / Math.max(1, t1 - t0)) * w;
    const y = (v: number) => HP.t + hh - ((v - lo) / Math.max(0.1, hi - lo)) * hh;
    return { lo, hi, w, hh, x, y, mins: (t1 - t0) / 60000, line: h.map((p) => `${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`).join(' ') };
  });
  let hover = $state<number | null>(null);
  function histMove(e: PointerEvent) {
    const h = globals.history, g = hist;
    if (!g) return;
    const r = (e.currentTarget as SVGElement).getBoundingClientRect();
    const px = e.clientX - r.left;
    let best = 0;
    for (let k = 1; k < h.length; k++) if (Math.abs(g.x(h[k].t) - px) < Math.abs(g.x(h[best].t) - px)) best = k;
    hover = best;
  }

  const v = (name: string) => globals.value(name);
  const s = (name: string) => globals.values?.[name];
  let lastBpm = 120;
  function tempo(onOff: boolean) {
    if (onOff) globals.set('tempo', v('tempo') || lastBpm);
    else {
      if (v('tempo')) lastBpm = v('tempo');
      globals.set('tempo', 0);
    }
  }

  /** a setting's value as words (the sidebar's summaries, the ranges' readouts) */
  function valueText(r: Row): string {
    const c = r.control, n = v(r.name);
    switch (c.kind) {
      case 'choice': return c.options[n] ?? '–';
      case 'switch': return n ? c.on : c.off;
      case 'list': return c.options.find(([x]) => x === n)?.[1] ?? '–';
      case 'range': return c.fmt ? c.fmt(n) : `${n}${c.unit ? ` ${c.unit}` : ''}`;
      case 'tempo': return n ? `${n} BPM` : 'Desligado';
      case 'exp': return `${v(`exp${c.pedal}_min`)} – ${v(`exp${c.pedal}_max`)}`;
    }
  }
  function summary(id: string): string {
    if (id === 'eq') return v('eq_on') ? 'Ligado' : 'Desligado';
    if (id === 'system') return globals.hasTemp ? `CPU ${deg(globals.temp('cpu_temp'))}` : `waveod ${studio.hello?.version ?? ''}`;
    const card = CARDS.find((c) => c.id === id)!;
    return (id === 'exp' ? card.rows : card.rows.slice(0, 2)).map(valueText).join(' · ');
  }

  const SECTIONS: { id: string; title: string; icon: IconName }[] = [
    { id: 'eq', title: 'EQ global', icon: 'sliders' },
    ...CARDS.map((c) => ({ id: c.id, title: c.title, icon: c.icon })),
    { id: 'system', title: 'Sistema', icon: 'chip' },
  ];
  const card = $derived<Card | undefined>(CARDS.find((c) => c.id === globals.section));
  const current = $derived(SECTIONS.find((x) => x.id === globals.section) ?? SECTIONS[0]);

  function navKey(e: KeyboardEvent) {
    const d = e.key === 'ArrowDown' ? 1 : e.key === 'ArrowUp' ? -1 : 0;
    if (!d) return;
    e.preventDefault();
    const i = SECTIONS.findIndex((x) => x.id === globals.section);
    const next = SECTIONS[(i + d + SECTIONS.length) % SECTIONS.length];
    globals.section = next.id;
    (e.currentTarget as HTMLElement).querySelector<HTMLElement>(`[data-id="${next.id}"]`)?.focus();
  }
</script>

<div class="screen">
  <header class="tools">
    <div class="title">
      <h1>Global</h1>
      <span class="sub">Os ajustes da tela GLOBAL do pedal: soam na hora, e o pedal os guarda dois segundos depois da última mudança.</span>
    </div>
    {#if globals.sending}<span class="busy">enviando…</span>{/if}
  </header>

  {#if globals.error}
    <div class="old">
      <p>{globals.error}</p>
      <button class="primary" onclick={() => studio.show('pedal')}>Ir à aba Pedal</button>
    </div>
  {:else if !globals.values}
    <p class="loading">Lendo os ajustes do pedal…</p>
  {:else}
    <div class="body">
      <div class="sections" role="tablist" aria-label="Seções" aria-orientation="vertical" tabindex="-1" onkeydown={navKey}>
        {#each SECTIONS as x (x.id)}
          <button class="sec" class:on={globals.section === x.id} data-id={x.id} role="tab" aria-selected={globals.section === x.id}
                  tabindex={globals.section === x.id ? 0 : -1}
                  onclick={() => (globals.section = x.id)}>
            <span class="ic"><Icon name={x.icon} size={19} stroke={1.9} /></span>
            <span class="tx"><b>{x.title}</b><small>{summary(x.id)}</small></span>
          </button>
        {/each}
      </div>

      <div class="page" role="tabpanel" aria-label={current.title}>
        {#if globals.section === 'eq'}
          <EqPanel />
        {:else if globals.section === 'system'}
          <header class="head">
            <h2><Icon name="chip" size={22} stroke={1.9} /> Sistema</h2>
            <p>{ABOUT.system}</p>
          </header>
          {#if !globals.hasTemp}
            <p class="note">O waveod do pedal ({studio.hello?.version ?? '?'}) ainda não mede a temperatura: grave um firmware mais novo.</p>
          {:else}
            <div class="temp {tone}">
              <div class="big"><b>{deg(now)}</b><span>processador agora</span></div>
              <dl>
                <div><dt>Máxima</dt><dd>{deg(tmax)}</dd></div>
                <div><dt>Mínima</dt><dd>{deg(tmin)}</dd></div>
              </dl>
            </div>
            <div class="tbar {tone}" role="meter" aria-label="Temperatura do processador" aria-valuemin={TEMP_LO} aria-valuemax={TEMP_HI}
                 aria-valuenow={now ?? undefined}>
              {#if tmin !== null && tmax !== null}<span class="span" style:left="{at(tmin)}%" style:width="{Math.max(0.5, at(tmax) - at(tmin))}%" title="Mínima a máxima desde que o pedal ligou"></span>{/if}
              {#if now !== null}<span class="fill" style:width="{at(now)}%"></span>{/if}
              <i class="limit" style:left="{at(85)}%" title="85 °C: perto do limite"></i>
            </div>
            <div class="tscale" aria-hidden="true"><span>{TEMP_LO} °C</span><span class="mark" style:left="{at(85)}%">85</span><span>{TEMP_HI} °C</span></div>
            <p class="hint">Máxima e mínima desde que o pedal ligou. A temperatura vem do sensor do próprio processador (i.MX RT1062), calibrado de fábrica; é a do chip, mais quente que a da caixa.</p>

            <figure class="hist">
              <figcaption>Desde que esta seção abriu{hist ? ` · ${Math.max(1, Math.round(hist.mins))} min` : ''}</figcaption>
              <div class="plot" bind:clientWidth={histW}>
              {#if hist}
                <svg width={Math.max(100, histW)} height={HH} role="img" aria-label="Temperatura nas últimas leituras"
                     onpointermove={histMove} onpointerleave={() => (hover = null)}>
                  <line class="grid" x1={HP.l} x2={HP.l + hist.w} y1={hist.y(hist.hi)} y2={hist.y(hist.hi)} />
                  <line class="grid" x1={HP.l} x2={HP.l + hist.w} y1={hist.y(hist.lo)} y2={hist.y(hist.lo)} />
                  <text class="ax" x={HP.l - 6} y={hist.y(hist.hi) + 4} text-anchor="end">{hist.hi} °C</text>
                  <text class="ax" x={HP.l - 6} y={hist.y(hist.lo) + 4} text-anchor="end">{hist.lo} °C</text>
                  <polyline class="line" points={hist.line} />
                  {#if hover !== null && globals.history[hover]}
                    {@const p = globals.history[hover]}
                    <line class="cross" x1={hist.x(p.t)} x2={hist.x(p.t)} y1={HP.t} y2={HP.t + hist.hh} />
                    <circle class="dot" cx={hist.x(p.t)} cy={hist.y(p.v)} r="4" />
                  {/if}
                </svg>
                {#if hover !== null && globals.history[hover]}
                  {@const p = globals.history[hover]}
                  <span class="htip" style:left="{Math.min(hist.x(p.t), Math.max(100, histW) - 160)}px">{new Date(p.t).toLocaleTimeString('pt-BR')} · {deg(p.v)}</span>
                {/if}
              {:else}
                <p class="hint">Lendo… (uma leitura a cada 2 s)</p>
              {/if}
              </div>
            </figure>
          {/if}
        {:else if card}
          <header class="head">
            <h2><Icon name={card.icon} size={22} stroke={1.9} /> {card.title}</h2>
            <p>{ABOUT[card.id]}</p>
          </header>
          <div class="rows">
            {#each card.rows as r (r.name)}
              {@const c = r.control}
              <div class="row">
                <div class="what">
                  <span class="t">{r.title}</span>
                  {#if r.hint}<span class="h">{r.hint}</span>{/if}
                </div>
                <div class="ctl">
                  {#if c.kind === 'choice' && c.options.length <= 4}
                    <div class="seg" role="radiogroup" aria-label={r.title}>
                      {#each c.options as o, n (n)}
                        <button role="radio" aria-checked={v(r.name) === n} class:on={v(r.name) === n}
                                onclick={() => globals.set(r.name, n)}>{o}</button>
                      {/each}
                    </div>
                  {:else if c.kind === 'choice'}
                    <select value={v(r.name)} aria-label={r.title} onchange={(e) => globals.set(r.name, Number(e.currentTarget.value))}>
                      {#each c.options as o, n (n)}<option value={n}>{o}</option>{/each}
                    </select>
                  {:else if c.kind === 'switch'}
                    <div class="seg" role="radiogroup" aria-label={r.title}>
                      <button role="radio" aria-checked={!v(r.name)} class:on={!v(r.name)} onclick={() => globals.set(r.name, 0)}>{c.off}</button>
                      <button role="radio" aria-checked={!!v(r.name)} class:on={!!v(r.name)} onclick={() => globals.set(r.name, 1)}>{c.on}</button>
                    </div>
                  {:else if c.kind === 'list'}
                    <select value={v(r.name)} aria-label={r.title} onchange={(e) => globals.set(r.name, Number(e.currentTarget.value))}>
                      {#each c.options as [val, label] (val)}<option value={val}>{label}</option>{/each}
                    </select>
                  {:else if c.kind === 'range' && s(r.name)}
                    {@const x = s(r.name)!}
                    <div class="range">
                      <input type="range" min={x.min} max={x.max} step={x.step} value={x.value} aria-label={r.title}
                             oninput={(e) => globals.set(r.name, Number(e.currentTarget.value))} />
                      <b>{valueText(r)}</b>
                    </div>
                  {:else if c.kind === 'tempo'}
                    <div class="range">
                      <div class="seg" role="radiogroup" aria-label={r.title}>
                        <button role="radio" aria-checked={!v('tempo')} class:on={!v('tempo')} onclick={() => tempo(false)}>Desligado</button>
                        <button role="radio" aria-checked={!!v('tempo')} class:on={!!v('tempo')} onclick={() => tempo(true)}>Ligado</button>
                      </div>
                      {#if v('tempo')}
                        <input type="range" min="40" max="300" step="1" value={v('tempo')} aria-label="BPM"
                               oninput={(e) => globals.set('tempo', Number(e.currentTarget.value))} />
                        <b>{v('tempo')} BPM</b>
                      {/if}
                    </div>
                  {:else if c.kind === 'exp'}
                    {@const lo = v(`exp${c.pedal}_min`)}
                    {@const hi = v(`exp${c.pedal}_max`)}
                    <div class="range">
                      <span class="cal" aria-hidden="true"><i style:left="{(lo / 255) * 100}%" style:width="{Math.max(2, ((hi - lo) / 255) * 100)}%"></i></span>
                      <b>{lo} – {hi}</b>
                    </div>
                  {/if}
                </div>
              </div>
            {/each}
          </div>
          {#if card.note}<p class="note">{card.note}</p>{/if}
        {/if}
      </div>
    </div>
  {/if}
</div>

<style>
  .screen { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .tools { display: flex; align-items: center; gap: 16px; padding: 16px 24px 14px; border-bottom: 1px solid var(--line); }
  .title { display: flex; flex-direction: column; gap: 4px; margin-right: auto; min-width: 0; }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  .sub { font-size: 13px; color: var(--label); }
  .busy { font: 500 12.5px var(--f-value); color: var(--accent); }
  .loading { margin: 40px auto; color: var(--label); }
  .old { margin: 40px auto; max-width: 560px; text-align: center; color: var(--label); line-height: 1.55; }
  .old .primary {
    height: 36px; padding: 0 16px; border-radius: var(--r-sm); background: var(--accent); color: var(--accent-ink);
    font: 700 15px var(--f-display);
  }

  .body { flex: 1; min-height: 0; display: flex; }
  .sections {
    flex: none; width: 272px; overflow-y: auto; padding: 12px 10px; background: var(--surface);
    border-right: 1px solid var(--line); display: flex; flex-direction: column; gap: 2px;
  }
  .sec {
    display: flex; align-items: center; gap: 12px; width: 100%; padding: 9px 12px; border-radius: var(--r-sm);
    text-align: left; color: var(--text);
  }
  .sec:hover:not(.on) { background: var(--raised); }
  .sec.on { background: var(--raised); box-shadow: inset 3px 0 0 var(--accent); }
  .ic { flex: none; width: 34px; height: 34px; display: grid; place-items: center; border-radius: 9px; background: var(--bg); color: var(--label); }
  .sec.on .ic { color: var(--accent); }
  .tx { display: flex; flex-direction: column; min-width: 0; }
  .tx b { font: 600 15.5px var(--f-display); }
  .tx small { font-size: 12px; color: var(--label); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

  .page { flex: 1; min-width: 0; overflow-y: auto; padding: 22px 28px 32px; }
  .page :global(.eq) { max-width: 1100px; }
  .head { margin-bottom: 14px; max-width: 760px; }
  h2 { display: flex; align-items: center; gap: 10px; margin: 0; font: 700 26px var(--f-display); }
  h2 :global(svg) { color: var(--accent); }
  .head p { margin: 6px 0 0; font-size: 13.5px; line-height: 1.5; color: var(--label); }
  .rows { max-width: 760px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); padding: 4px 20px; }
  .row { display: flex; align-items: center; justify-content: space-between; gap: 20px; min-height: 60px; padding: 10px 0; border-top: 1px solid var(--line); }
  .row:first-child { border-top: 0; }
  .what { display: flex; flex-direction: column; gap: 2px; min-width: 0; }
  .t { font: 600 16px var(--f-display); }
  .h { font-size: 12.5px; color: var(--label); }
  .ctl { flex: none; }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--bg); border: 1px solid var(--line); }
  .seg button { height: 30px; padding: 0 12px; border-radius: 7px; font: 600 14px var(--f-display); color: var(--label); white-space: nowrap; }
  .seg button:hover:not(.on) { color: var(--text); background: var(--track); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  select {
    height: 34px; min-width: 180px; padding: 0 8px; border-radius: var(--r-sm); background: var(--bg); color: var(--text);
    border: 1px solid var(--border-off); font: 500 14px var(--f-body); cursor: pointer;
  }
  .range { display: flex; align-items: center; gap: 12px; }
  .range input { width: 200px; accent-color: var(--accent); cursor: pointer; }
  .range b { min-width: 70px; text-align: right; font: 500 14px var(--f-value); }
  .cal { position: relative; width: 200px; height: 8px; border-radius: 4px; background: var(--track); }
  .cal i { position: absolute; top: 0; bottom: 0; border-radius: 4px; background: var(--label); }
  .note { max-width: 760px; margin: 10px 2px 0; font-size: 13px; color: var(--label); }

  /* SISTEMA */
  .temp { display: flex; align-items: flex-end; gap: 40px; max-width: 760px; margin: 8px 0 16px; }
  .temp .big { display: flex; flex-direction: column; gap: 4px; }
  .temp .big b { font: 700 56px/1 var(--f-display); color: var(--text); }
  .temp .big span { font: 500 13px var(--f-value); color: var(--label); }
  .temp.warm .big b { color: #f6c15a; }
  .temp.hot .big b { color: var(--error); }
  .temp dl { display: flex; gap: 32px; margin: 0 0 6px; }
  .temp dl div { display: flex; flex-direction: column; gap: 3px; }
  .temp dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .temp dd { margin: 0; font: 500 18px var(--f-value); }
  .tbar { position: relative; max-width: 760px; height: 14px; border-radius: 7px; background: var(--track); }
  .tbar .span { position: absolute; top: 0; bottom: 0; border-radius: 7px; background: var(--border-off); }
  .tbar .fill { position: absolute; left: 0; top: 4px; bottom: 4px; border-radius: 3px; background: var(--accent); }
  .tbar.warm .fill { background: #f6c15a; }
  .tbar.hot .fill { background: var(--error); }
  .tbar .limit { position: absolute; top: -4px; bottom: -4px; width: 2px; margin-left: -1px; background: var(--error); border-radius: 1px; opacity: 0.7; }
  .tscale { position: relative; display: flex; justify-content: space-between; max-width: 760px; margin: 5px 0 0; font: 500 11px var(--f-value); color: var(--off); }
  .tscale .mark { position: absolute; transform: translateX(-50%); color: var(--error); opacity: 0.8; }
  .hint { max-width: 760px; margin: 12px 2px 0; font-size: 12.5px; line-height: 1.5; color: var(--label); }
  .hist { position: relative; max-width: 760px; margin: 22px 0 0; padding: 14px 16px 10px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); }
  .hist figcaption { font: 600 12px var(--f-display); letter-spacing: 0.06em; text-transform: uppercase; color: var(--label); margin-bottom: 6px; }
  .hist .plot { position: relative; }
  .hist svg { display: block; }
  .hist .grid { stroke: var(--line); stroke-width: 1; }
  .hist .ax { font: 500 11px var(--f-value); fill: var(--label); }
  .hist .line { fill: none; stroke: var(--accent); stroke-width: 2; stroke-linejoin: round; }
  .hist .cross { stroke: var(--label); stroke-width: 1; }
  .hist .dot { fill: var(--accent); stroke: var(--surface); stroke-width: 2; }
  .htip {
    position: absolute; top: 4px; margin-left: 10px; white-space: nowrap; pointer-events: none; padding: 4px 8px;
    border-radius: var(--r-sm); background: var(--raised); border: 1px solid var(--border-off); font: 500 12px var(--f-value);
  }
  @media (max-width: 1240px) { .sections { width: 230px; } .range input, .cal { width: 150px; } }
</style>
