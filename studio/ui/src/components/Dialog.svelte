<script lang="ts">
  // The questions the app asks (studio.ask) and the progress of a long task (backup, restore), in the middle of
  // the window over a dimmed page.
  import { studio } from '../lib/store.svelte';

  const d = $derived(studio.dialog);
  const p = $derived(studio.progress);
  const pct = $derived(p && p.total ? Math.round((100 * p.done) / p.total) : 0);

  function focusFirst(node: HTMLElement) {
    node.querySelector<HTMLElement>('button.primary, button.danger, button')?.focus();
  }
  function key(e: KeyboardEvent) {
    if (e.key === 'Escape' && d) {
      e.stopPropagation();
      d.answer(null);
    }
  }
</script>

{#if d}
  <div class="veil" role="presentation" onclick={() => d.answer(null)}></div>
  <div class="box" role="alertdialog" aria-modal="true" aria-labelledby="dlg-title" aria-describedby="dlg-text"
       tabindex="-1" onkeydown={key} use:focusFirst>
    <h3 id="dlg-title">{d.title}</h3>
    <p id="dlg-text">{d.text}</p>
    <div class="actions">
      {#each d.actions as a (a.id)}
        <button class={a.kind ?? ''} onclick={() => d.answer(a.id)}>{a.label}</button>
      {/each}
    </div>
  </div>
{:else if p}
  <div class="veil" role="presentation"></div>
  <div class="box busy" role="status" aria-live="polite">
    <h3>{p.label}</h3>
    <p>{p.total ? `${p.done} de ${p.total} ${p.unit ?? 'presets'}` : 'Esperando o pedal…'}</p>
    <div class="bar" role="progressbar" aria-valuemin="0" aria-valuemax="100" aria-valuenow={pct}>
      <span style:transform="scaleX({p.total ? pct / 100 : 0.06})" class:wait={!p.total}></span>
    </div>
    {#if p.cancel}<div class="actions cancel"><button onclick={() => studio.cancelLong()}>Cancelar</button></div>{/if}
  </div>
{/if}

<style>
  .veil { position: fixed; inset: 0; z-index: 60; background: rgba(5, 5, 4, 0.62); animation: fade 0.18s; }
  .box {
    position: fixed;
    z-index: 61;
    left: 50%;
    top: 38%;
    transform: translate(-50%, -50%);
    width: min(480px, calc(100vw - 32px));
    padding: 20px 22px 18px;
    border-radius: var(--r-md);
    background: var(--raised);
    border: 1px solid var(--border-off);
    box-shadow: var(--float-shadow);
    animation: rise 0.24s var(--ease-out);
  }
  .box:focus { outline: none; }
  h3 { margin: 0 0 8px; font: 700 21px/1.2 var(--f-display); }
  p { margin: 0 0 18px; font-size: 14px; line-height: 1.5; color: var(--text); }
  .actions { display: flex; gap: 8px; flex-wrap: wrap; }
  .actions button {
    height: 36px;
    padding: 0 14px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 15px var(--f-display);
  }
  .actions button:hover { background: var(--track); }
  .actions .primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); }
  .actions .primary:hover { background: #f7c169; }
  .actions .danger { background: #3a1716; color: #ffd6d4; border-color: #6b2523; }
  .actions .danger:hover { background: #4a1c1b; }
  .busy p { color: var(--label); margin-bottom: 14px; font: 500 13px var(--f-value); }
  .cancel { margin-top: 16px; justify-content: flex-end; }
  .bar { height: 6px; border-radius: 3px; background: var(--track); overflow: hidden; }
  .bar span { display: block; height: 100%; background: var(--accent); transform-origin: left; transition: transform 0.2s; }
  .bar .wait { width: 6%; transform: none !important; animation: slide 1.1s ease-in-out infinite alternate; }
  @keyframes fade { from { opacity: 0; } }
  @keyframes rise { from { opacity: 0; transform: translate(-50%, calc(-50% + 8px)); } }
  @keyframes slide { from { translate: 0; } to { translate: 1567%; } }
</style>
