<script lang="ts">
  // A MIDI file on its way into a place: what the pedal will play (the converted score), what the file gave and
  // what it lost, the name on the pedal, the kick and snare kit, loop or once. Nothing is written until "Gravar".
  import Icon from './Icon.svelte';
  import DrumRoll from './DrumRoll.svelte';
  import { drums, pedalName } from '../lib/drums.svelte';

  const d = $derived(drums.draft!);
  const p = $derived(d.preview);
  const replaced = $derived(drums.state?.slots[d.place] ?? null);

  function setKit(kit: string) { if (kit !== p.kit) void drums.reconvert(kit, p.once); }
  function setOnce(once: boolean) { if (once !== p.once) void drums.reconvert(p.kit, once); }
  function key(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      e.stopPropagation();
      if (!d.busy) drums.draft = null;
    }
  }
  function focusName(node: HTMLInputElement) { node.focus(); node.select(); }
  const secs = (s: number) => (s >= 60 ? `${Math.floor(s / 60)} min ${Math.round(s % 60)} s` : `${s.toFixed(1)} s`);
</script>

<div class="veil" role="presentation" onclick={() => !d.busy && (drums.draft = null)}></div>
<div class="sheet" role="dialog" aria-modal="true" aria-labelledby="imp-title" tabindex="-1" onkeydown={key}>
  <header>
    <h3 id="imp-title">{replaced ? `Substituir M${d.place + 1}` : `Importar para M${d.place + 1}`}</h3>
    <span class="file"><Icon name="file" size={15} stroke={2} /> {p.file}</span>
  </header>

  <dl class="facts">
    <div><dt>Tempo</dt><dd>{p.bpm ? `${p.bpm} BPM` : 'sem tempo (fica o da bateria)'}</dd></div>
    <div><dt>Compassos</dt><dd>{p.bars} · {secs(p.seconds)}</dd></div>
    <div><dt>Notas</dt><dd>{p.notes}</dd></div>
    <div><dt>Eventos</dt><dd>{p.events} de {drums.state?.max_events ?? 2000}</dd></div>
  </dl>
  {#if p.dropped || p.cut || p.all_channels}
    <ul class="warn">
      {#if p.dropped}<li>Sem som no pedal, ficam de fora: {p.dropped}.</li>{/if}
      {#if p.cut}<li>Longo demais: só os primeiros {p.bars} compassos cabem ({p.cut} ficam de fora).</li>{/if}
      {#if p.all_channels}<li>O arquivo não usa o canal 10 da bateria: todas as notas foram lidas como bateria.</li>{/if}
    </ul>
  {/if}

  <div class="preview" class:busy={d.busy}>
    <DrumRoll roll={p} bar={p.bar} />
  </div>

  <div class="form">
    <label class="name">
      <span>Nome no pedal</span>
      <input value={d.name} maxlength="16" spellcheck="false" use:focusName
             oninput={(e) => { d.name = pedalName(e.currentTarget.value); e.currentTarget.value = d.name; }}
             onkeydown={(e) => e.key === 'Enter' && !d.busy && drums.commit()} />
    </label>
    <div class="field">
      <span>Bumbo e caixa</span>
      <div class="seg" role="radiogroup" aria-label="Bumbo e caixa">
        <button role="radio" aria-checked={p.kit === 'acustico'} class:on={p.kit === 'acustico'} onclick={() => setKit('acustico')}>Acústicos</button>
        <button role="radio" aria-checked={p.kit === 'eletronico'} class:on={p.kit === 'eletronico'} onclick={() => setKit('eletronico')}>Eletrônicos</button>
      </div>
    </div>
    <div class="field">
      <span>No fim</span>
      <div class="seg" role="radiogroup" aria-label="No fim">
        <button role="radio" aria-checked={!p.once} class:on={!p.once} onclick={() => setOnce(false)}>
          <Icon name="loop" size={14} stroke={2} /> Volta ao começo</button>
        <button role="radio" aria-checked={p.once} class:on={p.once} onclick={() => setOnce(true)}>
          <Icon name="once" size={14} stroke={2} /> Para (música inteira)</button>
      </div>
    </div>
  </div>

  <footer>
    <p class:lost={!!replaced}>
      {replaced ? `Isto substitui “${replaced.name}”, que está em M${d.place + 1}.` : `M${d.place + 1} está vazio.`}
    </p>
    <button onclick={() => (drums.draft = null)} disabled={d.busy}>Cancelar</button>
    <button class={replaced ? 'danger' : 'primary'} onclick={() => drums.commit()} disabled={d.busy || !d.name.trim()}>
      Gravar em M{d.place + 1}
    </button>
  </footer>
</div>

<style>
  .veil { position: fixed; inset: 0; z-index: 60; background: rgba(5, 5, 4, 0.62); animation: fade 0.18s; }
  .sheet {
    position: fixed;
    z-index: 61;
    left: 50%;
    top: 50%;
    transform: translate(-50%, -50%);
    width: min(940px, calc(100vw - 48px));
    max-height: calc(100vh - 48px);
    overflow-y: auto;
    padding: 22px 24px 18px;
    border-radius: 14px;
    background: var(--raised);
    border: 1px solid var(--border-off);
    box-shadow: var(--float-shadow);
    animation: rise 0.24s var(--ease-out);
  }
  .sheet:focus { outline: none; }
  header { display: flex; align-items: baseline; gap: 14px; margin-bottom: 14px; }
  h3 { margin: 0; font: 700 24px/1.1 var(--f-display); }
  .file { display: flex; align-items: center; gap: 6px; font: 500 13px var(--f-value); color: var(--label); min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .facts { display: flex; gap: 32px; margin: 0 0 12px; flex-wrap: wrap; }
  .facts dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .facts dd { margin: 3px 0 0; font: 500 14.5px var(--f-value); }
  .warn { margin: 0 0 12px; padding: 9px 14px 9px 30px; border-radius: var(--r-sm); background: #2a1f0c; border: 1px solid #5a4114; color: #f6dcae; font-size: 13.5px; line-height: 1.5; }
  .preview { margin-bottom: 16px; transition: opacity 0.15s; }
  .preview.busy { opacity: 0.45; }
  .form { display: flex; gap: 22px; flex-wrap: wrap; align-items: flex-end; margin-bottom: 18px; }
  .name, .field { display: flex; flex-direction: column; gap: 6px; }
  .name span, .field > span { font: 600 12px var(--f-display); letter-spacing: 0.06em; text-transform: uppercase; color: var(--label); }
  .name input {
    width: 250px;
    height: 36px;
    padding: 0 10px;
    border-radius: var(--r-sm);
    background: var(--bg);
    border: 1px solid var(--border-off);
    outline: none;
    font: 600 17px var(--f-display);
    user-select: text;
  }
  .name input:focus { border-color: var(--accent); }
  .seg { display: flex; padding: 3px; gap: 2px; border-radius: var(--r-md); background: var(--surface); border: 1px solid var(--line); }
  .seg button { display: flex; align-items: center; gap: 6px; height: 30px; padding: 0 12px; border-radius: 7px; font: 600 14.5px var(--f-display); color: var(--label); }
  .seg button:hover:not(.on) { color: var(--text); background: var(--track); }
  .seg button.on { background: var(--accent); color: var(--accent-ink); }
  footer { display: flex; align-items: center; gap: 8px; }
  footer p { flex: 1; margin: 0; font-size: 13.5px; color: var(--label); }
  footer p.lost { color: #f6dcae; }
  footer button { height: 36px; padding: 0 16px; border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 600 15px var(--f-display); }
  footer button:hover:not(:disabled) { background: var(--track); }
  footer button:disabled { opacity: 0.5; cursor: default; }
  footer .primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); font-weight: 700; }
  footer .primary:hover:not(:disabled) { background: #f7c169; }
  footer .danger { background: #3a1716; color: #ffd6d4; border-color: #6b2523; }
  footer .danger:hover:not(:disabled) { background: #4a1c1b; }
  @keyframes fade { from { opacity: 0; } }
  @keyframes rise { from { opacity: 0; transform: translate(-50%, calc(-50% + 8px)); } }
</style>
