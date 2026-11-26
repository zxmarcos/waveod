<script lang="ts">
  // The picked preset as it is stored: name (renamed in place), facts, its chain on the grid, its blocks in
  // signal order, its scenes; and what can be done with it.
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import MiniGrid from './MiniGrid.svelte';
  import { menuInfo } from '../lib/menus';
  import { ctl } from '../lib/ctl.svelte';
  import { isVacant, presetLabel, studio } from '../lib/store.svelte';
  import type { PresetDetail } from '../lib/types';

  const ROUTES = ['Série', 'Paralelo', 'Dual L/R'];
  const ORIGINS: Record<number, string> = { 0: 'usuário', 1: 'fábrica' };

  const i = $derived(studio.picked);
  const entry = $derived(i === null ? undefined : studio.presets[i]);
  const d = $derived(i === null ? undefined : studio.details[i]);
  const vacant = $derived(isVacant(entry));
  const live = $derived(!!entry?.active);
  const dirty = $derived(live && !!studio.rig?.dirty);

  const nameOf = (b: PresetDetail['blocks'][number]) =>
    studio.modelName(b.menu, b.id, b.index) ?? menuInfo(b.menu).name;

  // ---------------------------------------------------------------- rename in place
  let draft = $state('');
  let input = $state<HTMLInputElement>();
  $effect(() => {
    if (studio.renaming && input) {
      draft = untrack(() => entry?.name ?? '');
      input.focus();
      input.select();
    }
  });
  function renameKey(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      e.preventDefault();
      studio.renaming = false;
      if (i !== null) void studio.renamePreset(i, draft);
    } else if (e.key === 'Escape') {
      e.stopPropagation();
      studio.renaming = false;
    }
  }

  async function edit() {
    if (i !== null && (await studio.activate(i))) studio.show('grid');
  }
  function place(op: 'move' | 'copy') {
    if (i !== null) studio.placing = { op, src: i };
  }
</script>

<aside class="detail" aria-label="Detalhes do preset">
  {#if i === null || !entry}
    <p class="empty">Escolha um preset na lista.</p>
  {:else}
    <div class="scroll">
      <header>
        <span class="label">{presetLabel(i)}</span>
        {#if live}
          <span class="tag live" class:dirty>{dirty ? 'no pedal · alterado' : 'no pedal'}</span>
        {/if}
        {#if !vacant}
          <span class="tag">{entry.place === 1 ? `original · ${ORIGINS[entry.origin] ?? '?'}` : 'waveod'}</span>
        {/if}
      </header>

      {#if studio.renaming && !vacant}
        <input class="rename" bind:this={input} bind:value={draft} maxlength="17" spellcheck="false"
               aria-label="Nome do preset (até 17 caracteres)" onkeydown={renameKey} onblur={() => (studio.renaming = false)} />
        <p class="hint">Enter grava · Esc cancela · até 17 caracteres</p>
      {:else}
        <h2 class:vacant>{vacant ? 'Lugar vazio' : entry.name || 'sem nome'}</h2>
      {/if}

      {#if vacant}
        <p class="note">Nada guardado aqui. Arraste um preset da lista para este lugar (Ctrl copia) ou importe um arquivo .wvp.</p>
      {:else if !d}
        <p class="note">Lendo o preset…</p>
      {:else}
        {#if dirty}
          <p class="note warn">O pedal tem alterações deste preset que não foram salvas; aqui está o que está guardado.</p>
        {/if}
        <dl class="facts">
          <div><dt>Roteamento</dt><dd>{ROUTES[d.route.mode] ?? '?'}</dd></div>
          <div><dt>BPM</dt><dd>{d.bpm ?? '–'}</dd></div>
          <div><dt>Volume</dt><dd>{d.volume ?? '–'}</dd></div>
          <div><dt>Estilo</dt><dd>{d.style || '–'}</dd></div>
        </dl>

        <h3>Cadeia <span>{d.blocks.length} {d.blocks.length === 1 ? 'bloco' : 'blocos'}</span></h3>
        {#if d.blocks.length}
          <MiniGrid detail={d} names={nameOf} />
          <ol class="blocks">
            {#each d.blocks as b (b.slot)}
              {@const m = menuInfo(b.menu)}
              <li class:off={!b.on} style:--c={m.color}>
                <Icon name={m.icon} size={17} stroke={2} />
                <span class="bn">{nameOf(b)}</span>
                <span class="bm">{b.on ? m.name : 'desligado'}</span>
              </li>
            {/each}
          </ol>
        {:else}
          <p class="note">Sem blocos.</p>
        {/if}

        <h3>Cenas</h3>
        {#if d.scenes.own}
          <ul class="scenes">
            {#each d.scenes.names as name, n (n)}
              <li class:cur={d.scenes.active === n}>
                <span class="sn">C{n + 1}</span>
                <span class="sname">{name || `Cena ${n + 1}`}</span>
                <span class="marks" aria-label="{d.scenes.on[n].length} blocos ligados">
                  {#each d.blocks as b (b.slot)}
                    <i class:lit={d.scenes.on[n].includes(b.slot)} style:--c={menuInfo(b.menu).color}></i>
                  {/each}
                </span>
              </li>
            {/each}
          </ul>
          {#if d.scenes.params}
            <p class="more">{d.scenes.params} {d.scenes.params === 1 ? 'parâmetro muda' : 'parâmetros mudam'} de valor por cena</p>
          {/if}
        {:else}
          <p class="note">Sem cenas próprias: as quatro tocam os blocos como estão.</p>
        {/if}
        <h3>Controles</h3>
        <div class="ctlrow">
          <p class="note">
            {d.controls ? 'Tabela própria' : 'Sem tabela própria (campos de footswitch do formato original)'}{d.mods
              ? ` · ${d.mods} ${d.mods === 1 ? 'modulador' : 'moduladores'}` : ''}
          </p>
          <button onclick={() => i !== null && ctl.openPreset(i)} title="Footswitches, pedais, MIDI e moduladores deste preset">
            <Icon name="link" size={15} stroke={2.1} /> Editar controles…
          </button>
        </div>
      {/if}
    </div>

    <footer class="actions">
      {#if vacant}
        <button class="primary wide" onclick={() => studio.importPreset(i)}>Importar .wvp para cá…</button>
      {:else}
        <button class="primary wide" onclick={edit}>
          <Icon name="grid" size={16} stroke={2.1} /> {live ? 'Editar na grade' : 'Ativar e editar'}
        </button>
        {#if !live}
          <button class="wide" onclick={() => studio.activate(i)} title="Duplo clique na lista faz o mesmo">Ativar no pedal</button>
        {/if}
        <button onclick={() => (studio.renaming = true)} title="F2">Renomear</button>
        <button onclick={() => place('move')}>Mover para…</button>
        <button onclick={() => place('copy')} title="Ctrl+C e Ctrl+V na lista fazem o mesmo">Copiar para…</button>
        <button onclick={() => studio.exportPreset(i)}>Exportar .wvp…</button>
        <button class="wide" onclick={() => studio.importPreset(i)}>Importar .wvp por cima…</button>
      {/if}
    </footer>
  {/if}
</aside>

<style>
  .detail {
    flex: none;
    width: 412px;
    display: flex;
    flex-direction: column;
    border-left: 1px solid var(--line);
    background: var(--surface);
  }
  .scroll { flex: 1; overflow-y: auto; padding: 18px 22px 8px; }
  .empty { margin: auto; color: var(--label); }
  header { display: flex; align-items: center; gap: 8px; }
  .label { font: 500 20px var(--f-value); color: var(--accent); margin-right: 4px; }
  .tag {
    font: 600 11.5px/1 var(--f-display);
    letter-spacing: 0.06em;
    text-transform: uppercase;
    padding: 4px 7px;
    border-radius: 5px;
    border: 1px solid var(--border-off);
    color: var(--label);
  }
  .tag.live { color: var(--ok); border-color: #2d5a37; }
  .tag.live.dirty { color: var(--accent); border-color: #5a4114; }
  h2 { margin: 8px 0 14px; font: 700 30px/1.1 var(--f-display); overflow-wrap: anywhere; }
  h2.vacant { color: var(--off); }
  .rename {
    width: 100%;
    margin: 8px 0 4px;
    height: 44px;
    padding: 0 10px;
    border-radius: var(--r-sm);
    background: var(--bg);
    border: 1px solid var(--accent);
    outline: none;
    font: 700 26px var(--f-display);
    user-select: text;
  }
  .hint { margin: 0 0 12px; font-size: 12px; color: var(--label); }
  .note { margin: 0 0 14px; color: var(--label); font-size: 13.5px; line-height: 1.5; }
  .note.warn { color: #f6dcae; }
  .facts { display: grid; grid-template-columns: repeat(4, auto); justify-content: space-between; margin: 0 0 18px; }
  .facts dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .facts dd { margin: 2px 0 0; font: 500 15px var(--f-value); }
  h3 {
    display: flex;
    align-items: baseline;
    gap: 8px;
    margin: 0 0 10px;
    font: 600 12.5px var(--f-display);
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--label);
  }
  h3 span { font: 500 12px var(--f-value); letter-spacing: 0; text-transform: none; color: var(--dim); }
  .blocks { list-style: none; margin: 12px 0 20px; padding: 0; }
  .blocks li {
    display: flex;
    align-items: center;
    gap: 10px;
    height: 30px;
    color: var(--c);
    border-bottom: 1px solid var(--line);
  }
  .blocks li.off { color: var(--off); }
  .bn { flex: 1; min-width: 0; font: 600 15px var(--f-display); color: var(--text); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .off .bn { color: var(--label); }
  .bm { font: 600 12px var(--f-display); letter-spacing: 0.04em; }
  .scenes { list-style: none; margin: 0 0 8px; padding: 0; }
  .scenes li { display: flex; align-items: center; gap: 10px; height: 28px; }
  .sn { font: 500 13px var(--f-value); color: var(--label); width: 22px; }
  .cur .sn { color: var(--accent); }
  .sname { width: 104px; font: 600 14.5px var(--f-display); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .marks { display: flex; gap: 3px; }
  .marks i { width: 12px; height: 8px; border-radius: 2px; box-shadow: inset 0 0 0 1px var(--border-off); }
  .marks i.lit { background: var(--c); box-shadow: none; }
  .more { margin: 4px 0 14px; font-size: 12.5px; color: var(--label); }
  .ctlrow { display: flex; align-items: center; gap: 12px; margin-bottom: 14px; }
  .ctlrow .note { flex: 1; margin: 0; }
  .ctlrow button {
    flex: none; display: flex; align-items: center; gap: 6px; height: 32px; padding: 0 12px;
    border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 600 14.5px var(--f-display);
  }
  .ctlrow button:hover { background: var(--raised); }

  .actions {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
    padding: 14px 22px 18px;
    border-top: 1px solid var(--line);
  }
  .actions button {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 7px;
    height: 36px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 15px var(--f-display);
    color: var(--text);
  }
  .actions button:hover { background: var(--raised); }
  .actions .wide { grid-column: 1 / -1; }
  .actions .primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); font-weight: 700; }
  .actions .primary:hover { background: #f7c169; }
</style>
