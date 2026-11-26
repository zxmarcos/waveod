<script lang="ts">
  import { studio } from '../lib/store.svelte';

  let shown = $state<typeof studio.toast>(null);
  $effect(() => {
    const t = studio.toast;
    if (!t) return;
    shown = t;
    const ms = t.kind === 'error' ? 5200 : t.action ? 7000 : 2800;
    const id = setTimeout(() => { if (shown === t) shown = null; }, ms);
    return () => clearTimeout(id);
  });
  function act() {
    const a = shown?.action;
    shown = null;
    a?.run();
  }
</script>

{#if shown}
  {#key shown.at}
    <div class="toast" class:error={shown.kind === 'error'} class:low={studio.screen !== 'grid'}
         role={shown.kind === 'error' ? 'alert' : 'status'}>
      <span>{shown.text}</span>
      {#if shown.action}<button onclick={act}>{shown.action.label}</button>{/if}
    </div>
  {/key}
{/if}

<style>
  .toast {
    position: fixed;
    left: 50%;
    bottom: 254px;
    transform: translateX(-50%);
    z-index: 50;
    display: flex;
    align-items: center;
    gap: 14px;
    max-width: 620px;
    padding: 10px 12px 10px 16px;
    border-radius: var(--r-md);
    background: var(--raised);
    border: 1px solid var(--border-off);
    box-shadow: var(--float-shadow);
    font: 500 14px/1.4 var(--f-body);
    animation: rise 0.3s var(--ease-out);
  }
  .toast.low { bottom: 28px; }
  .toast.error { border-color: #6b2523; color: #ffd6d4; background: #2a1312; }
  button {
    flex: none;
    height: 30px;
    padding: 0 12px;
    border-radius: var(--r-sm);
    font: 700 14px var(--f-display);
    color: var(--accent);
  }
  button:hover { background: var(--track); }
  @keyframes rise { from { opacity: 0; transform: translate(-50%, 8px); } }
</style>
