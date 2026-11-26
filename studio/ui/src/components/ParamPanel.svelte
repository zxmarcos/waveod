<script lang="ts">
  import Icon from './Icon.svelte';
  import Knob from './Knob.svelte';
  import MicEditor from './MicEditor.svelte';
  import { menuInfo } from '../lib/menus';
  import { studio } from '../lib/store.svelte';
  import type { Param } from '../lib/types';

  const blk = $derived(studio.selectedBlock);
  const m = $derived(blk ? menuInfo(blk.menu) : null);
  const joint = $derived(studio.selection?.kind === 'joint' ? studio.selection.which : null);
  /** a Cab / IR block of waveod's engine: its knobs 8..11 are the virtual mic (src/engine/cabmic.h) */
  const hasMic = $derived(studio.params.some((p) => p.knob === 8 && /mic/i.test(p.name)));
  let micOpen = $state(false);
  $effect(() => { if (!hasMic) micOpen = false; });

  // the routing joints (src/fw/preset_format.h enum route_joint): 0..100 each
  const JOINTS = {
    split: [[0, 'Equilíbrio'], [1, 'Nível A'], [2, 'Nível B']],
    merge: [[3, 'Mistura'], [4, 'Pan A'], [5, 'Pan B']],
  } as const;

  /** the browser's group next to the menu, when it says more than the menu (not "-", not "Gabinetes") */
  const showType = (menu: number, type: string) =>
    !!type && type !== '-' && type !== '/' && menu !== 5 && menu !== 6 && type !== menuInfo(menu).name;

  function jointParam(k: number, name: string): Param {
    const v = studio.rig?.route.joints[k] ?? 50;
    const pan = k >= 4;
    const text = pan ? (v === 50 ? 'C' : v < 50 ? `L${(50 - v) * 2}` : `R${(v - 50) * 2}`) :
                 k === 0 ? `${100 - v} / ${v}` : `${v}%`;
    return { k, knob: k, synced: false, scene: false, min: 0, max: 100, step: 1, default: k === 1 || k === 2 ? 100 : 50,
             value: v, name, text, options: [] };
  }
</script>

<section class="panel" style:--c={m?.color ?? 'var(--accent)'}>
  {#if blk && m}
    <header>
      <span class="badge" class:off={!blk.on}><Icon name={m.icon} size={22} stroke={2} /></span>
      <div class="what">
        <span class="menu">{m.name}{showType(blk.menu, blk.type) ? ` · ${blk.type}` : ''}</span>
        <h2>{blk.name}</h2>
      </div>
      {#if hasMic}
        <button class="ghost mic" onclick={() => (micOpen = true)} title="Onde o microfone virtual fica na frente do alto-falante">
          <Icon name="mic" size={17} stroke={2} /> Microfone
        </button>
      {/if}
      <button class="switch" class:on={blk.on} role="switch" aria-checked={blk.on} onclick={() => studio.toggle(blk.slot)}>
        <Icon name="power" size={16} stroke={2.6} /> {blk.on ? 'Ligado' : 'Desligado'}
      </button>
      <button class="ghost" onclick={() => studio.remove(blk.slot)} title="Tirar o bloco da cadeia (Delete)">
        <Icon name="trash" size={17} stroke={2} /> Remover
      </button>
    </header>
    <div class="knobs" class:dimmed={!blk.on}>
      {#each studio.params as p (p.k)}
        <Knob param={p} color={blk.on ? m.color : 'var(--dim)'}
              onchange={(v) => studio.setParam(p.k, v)} onreset={() => studio.resetParam(p.k)}
              onhold={(h) => (studio.dragging = h)}
              onscene={studio.canEditScenes ? () => studio.sceneParam(blk.slot, p.k) : undefined} />
      {:else}
        <p class="note">Lendo os parâmetros…</p>
      {/each}
    </div>
  {:else if joint}
    <header>
      <span class="badge joint"></span>
      <div class="what">
        <span class="menu">Roteamento</span>
        <h2>{joint === 'split' ? 'Divisão' : 'Junção'}</h2>
      </div>
    </header>
    <div class="knobs">
      {#each JOINTS[joint] as [k, name] (k)}
        <Knob param={jointParam(k, name)} color="var(--accent)" onchange={(v) => studio.joint(k, v)}
              onreset={() => studio.joint(k, jointParam(k, name).default)} onhold={(h) => (studio.dragging = h)} />
      {/each}
    </div>
  {:else if studio.selection?.kind === 'cell'}
    <p class="empty-note">Lugar vazio. Escolha um modelo na lista à esquerda, ou arraste um para cá.</p>
  {:else}
    <div class="empty-note">
      <p>Selecione um bloco para ver os parâmetros.</p>
      <p class="keys">Duplo clique liga e desliga · arraste para mover · Delete remove</p>
    </div>
  {/if}
</section>

{#if micOpen && hasMic}<MicEditor onclose={() => (micOpen = false)} />{/if}

<style>
  .panel {
    flex: none;
    height: 236px;
    border-top: 1px solid var(--line);
    background: var(--surface);
    display: flex;
    flex-direction: column;
  }
  header {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 14px 20px 6px;
  }
  .badge {
    width: 40px;
    height: 40px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 12px;
    border: 2px solid var(--c);
    color: var(--c);
    background: var(--bg);
  }
  .badge.off { border-color: var(--border-off); color: var(--off); }
  .badge.joint { width: 22px; height: 22px; border-radius: 4px; transform: rotate(45deg); margin: 0 9px; }
  .what { display: flex; flex-direction: column; min-width: 0; flex: 1; }
  .menu {
    font: 600 12px/1.2 var(--f-display);
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--c);
  }
  h2 { margin: 0; font: 700 24px/1.1 var(--f-display); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .switch, .ghost {
    display: flex;
    align-items: center;
    gap: 7px;
    height: 34px;
    padding: 0 12px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 14px var(--f-display);
    color: var(--label);
  }
  .switch.on { color: var(--c); border-color: var(--c); }
  .switch:hover, .ghost:hover { background: var(--raised); color: var(--text); }
  .knobs {
    flex: 1;
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 0 14px 6px;
    overflow-x: auto;
    overflow-y: hidden;
  }
  .knobs.dimmed { opacity: 0.75; }
  .note { color: var(--label); margin: 0 6px; }
  .empty-note {
    margin: auto;
    text-align: center;
    color: var(--label);
    font-size: 14px;
  }
  .empty-note p { margin: 4px 0; }
  .keys { font-size: 12.5px; }
</style>
