<!--
  The pedal itself: which waveod it runs and what its memory holds, how long the sound takes through it (its own
  latency test, the same as GLOBAL › LATÊNCIA), and where its firmware is updated.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import { pedal, ROUTES } from '../lib/pedal.svelte';
  import { presetLabel, studio } from '../lib/store.svelte';

  $effect(() => { untrack(() => void pedal.enter()); });

  const i = $derived(pedal.info);
  const l = $derived(pedal.latency);
  const num = (v: number, n = 2) => v.toFixed(n).replace('.', ',');
  const usedPct = $derived(i && i.store.sectors ? (100 * (i.store.sectors - i.store.free)) / i.store.sectors : 0);
  const good = $derived(!!l && (l.status === 2 || l.status === 3));
  const why = $derived(!l ? '' : l.status === 4 ? (l.route === 3 ? 'o preset não deixou o sinal passar (gate? volume?)'
                                                                  : 'nada voltou: confira o cabo e a rota')
                        : l.status === 5 ? 'a entrada saturou já a -30 dBFS: use o tipo de entrada LINHA (GLOBAL)' : '');
  const rest = $derived(l ? l.mean - l.buffers : 0);

  let hover = $state(-1);
  const hex = (v: number) => v.toString(16).toUpperCase().padStart(6, '0');
  const kb = (n: number) => (n >= 1048576 ? `${(n / 1048576).toFixed(2).replace('.', ',')} MB` : `${Math.ceil(n / 1024)} KB`);
</script>

<div class="screen">
  <header class="tools"><h1>Pedal</h1></header>
  <div class="cards">
    <section class="card about" aria-label="Este pedal">
      <h2>Este pedal</h2>
      {#if i}
        <p class="model"><Icon name="pedal" size={18} stroke={2} /> {i.name}</p>
        <dl>
          <div><dt>Firmware</dt><dd>waveod {i.version}</dd></div>
          <div><dt>Protocolo</dt><dd>{i.protocol}</dd></div>
          <div><dt>Preset ativo</dt><dd>{presetLabel(studio.rig?.index ?? i.active)}{studio.rig?.dirty ? ' · alterado' : ''}</dd></div>
          <div><dt>Presets</dt><dd>{i.own} do waveod · {i.original} originais · {i.vacant} vazios</dd></div>
        </dl>
        <h3>Memória do waveod</h3>
        <div class="meter" role="meter" aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round(usedPct)}
             aria-label="Setores em uso">
          <span style:width="{Math.max(usedPct, 1)}%"></span>
        </div>
        <p class="small">{Math.round(i.store.live / 1024)} KB em uso · {i.store.free} de {i.store.sectors} setores livres.
          Presets, ritmos e controles do waveod ficam numa área que nenhuma atualização de firmware apaga.</p>
      {:else}
        <p class="small">Lendo o pedal…</p>
      {/if}
    </section>

    <section class="card fw" aria-label="Firmware">
      <h2>Atualizar o firmware</h2>
      <p class="small">Grava um .bin do waveod (ou o firmware original, para voltar) pela USB, como o editor original:
        o pedal reinicia no modo de atualização e recebe o firmware. Os presets do waveod, as capturas NAM e os IRs de
        usuário são mantidos; os presets originais voltam aos do arquivo. Faça um backup antes.</p>
      <div class="fwrow">
        <button class="primary" onclick={() => pedal.pickFirmware()}><Icon name="file" size={15} stroke={2} /> Escolher firmware…</button>
        <button onclick={() => studio.show('presets')}><Icon name="save" size={15} stroke={2} /> Ir ao backup</button>
      </div>
    </section>

    <section class="card lat" aria-label="Latência">
      <h2>Latência</h2>
      <p class="small">O pedal mede quanto o som leva da entrada à saída. Com um cabo da saída escolhida na entrada, o
        teste toca um clique nessa saída (desligue o amplificador e o fone). Sem cabo, mede quanto o preset ativo
        acrescenta. O mesmo teste fica em GLOBAL › LATÊNCIA no pedal.</p>
      <div class="row">
        <div class="seg" role="radiogroup" aria-label="Rota">
          {#each ROUTES as r (r.route)}
            <button role="radio" aria-checked={pedal.route === r.route} class:on={pedal.route === r.route}
                    onclick={() => (pedal.route = r.route)}>{r.label}</button>
          {/each}
        </div>
        <button class="primary" onclick={() => pedal.measure()}>Medir</button>
      </div>

      {#if l && good}
        <div class="result" class:unstable={l.status === 3}>
          <div class="big"><b>{num(l.ms, 2)}</b> ms</div>
          <div class="what">
            <p>{ROUTES[l.route]?.label} · {num(l.mean)} amostras a {num(l.rate / 1000, 1)} kHz{l.status === 3 ? ' · INSTÁVEL' : ''}</p>
            {#if l.route !== 3}
              <div class="split" aria-hidden="true">
                <span class="buf" style:flex={l.buffers}></span>
                <span class="conv" style:flex={Math.max(rest, 2)}></span>
              </div>
              <p class="small"><i class="key buf"></i>buffers {l.buffers} amostras ({num((l.buffers * 1000) / l.rate, 3)} ms)
                + <i class="key conv"></i>conversores {num(rest, 1)} amostras ({num((rest * 1000) / l.rate, 3)} ms)</p>
            {:else}
              <p class="small">o que o preset ativo soma à latência do pedal</p>
            {/if}
            <p class="small">{l.runs} medidas de {num(l.min)} a {num(l.max)}{l.route !== 3 ? ` · ${l.level_db} dBFS` : ''} ·
              semelhança {Math.round(l.match * 100)}%</p>
          </div>
        </div>
      {:else if l}
        <p class="fail">{ROUTES[l.route]?.label}: {l.status_text}{why ? ` · ${why}` : ''}</p>
      {:else}
        <p class="small none">Sem medição. Só os buffers dão 128 amostras (2,90 ms) a 44,1 kHz.</p>
      {/if}
    </section>

    <section class="card flash" aria-label="Cópia da flash">
      <h2>Cópia da flash</h2>
      <p class="small">Lê a memória flash do pedal para um arquivo, sem gravar nada nele. A cópia inteira guarda tudo
        (presets, ritmos, capturas, IRs, efeitos) e serve de backup completo; pela USB leva alguns minutos.</p>
      <div class="row">
        <div class="seg" role="radiogroup" aria-label="O que copiar">
          {#each [['all', 'Flash inteira (8 MB)'], ['boot', 'Bootloader (128 KB)']] as [k, label] (k)}
            <button role="radio" aria-checked={pedal.dumpKind === k} class:on={pedal.dumpKind === k}
                    onclick={() => (pedal.dumpKind = k as 'all' | 'boot')}>{label}</button>
          {/each}
        </div>
        <button class="primary" onclick={() => pedal.makeDump()}><Icon name="save" size={15} stroke={2} /> Copiar para arquivo…</button>
        <button class="ghost" onclick={() => pedal.openDump()} title="O resumo de uma cópia de 8 MB feita antes">
          <Icon name="file" size={15} stroke={2} /> Abrir cópia…</button>
      </div>

      {#if pedal.dump?.summary}
        {@const areas = pedal.dump.summary}
        <h3>{pedal.dump.file}</h3>
        <div class="map" role="img" aria-label="Mapa da flash: o quanto cada área usa">
          {#each areas as a, n (a.name)}
            <span class="area" class:hot={hover === n} style:flex={a.end - a.start} title="{a.name}: {kb(a.used)} de {kb(a.end - a.start)}"
                  onpointerenter={() => (hover = n)} onpointerleave={() => (hover = -1)} role="presentation">
              <i style:width="{Math.max(a.used ? 2 : 0, (100 * a.used) / (a.end - a.start))}%"></i>
            </span>
          {/each}
        </div>
        <div class="ticks" aria-hidden="true"><span>0</span><span>2 MB</span><span>4 MB</span><span>6 MB</span><span>8 MB</span></div>
        <table class="areas">
          <thead><tr><th>Área</th><th>Faixa</th><th>Em uso</th><th>Último byte</th></tr></thead>
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
