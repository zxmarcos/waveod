<script lang="ts">
  // A firmware .bin on its way into the pedal: what it is, what an update rewrites and keeps, and an explicit "I
  // understand" before the pedal is rebooted into its bootloader and written.
  import Icon from './Icon.svelte';
  import { pedal } from '../lib/pedal.svelte';

  const f = $derived(pedal.fw!);
  const mb = (n: number) => (n >= 1048576 ? `${(n / 1048576).toFixed(1).replace('.', ',')} MB` : `${Math.round(n / 1024)} KB`);
  function take(node: HTMLElement) { node.focus(); }
  function key(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      e.stopPropagation();
      pedal.fw = null;
    }
  }
</script>

<div class="veil" role="presentation" onclick={() => (pedal.fw = null)}></div>
<div class="sheet" role="dialog" aria-modal="true" aria-labelledby="fw-title" tabindex="-1" onkeydown={key} use:take>
  <h3 id="fw-title">Atualizar o firmware do pedal</h3>
  <p class="file"><Icon name="file" size={15} stroke={2} /> {f.file} · {f.version} · {mb(f.size)}</p>
  <ul class="secs">
    {#each f.sections as s (s.name)}<li><span>{s.name}</span><b>{mb(s.size)}</b></li>{/each}
  </ul>
  <ul class="notes">
    <li>O pedal reinicia no modo de atualização (bootloader) e recebe as quatro seções pela USB, como no editor original.
      O bootloader nunca é regravado: uma gravação que falhe pode ser repetida.</li>
    <li>Os presets, ritmos e controles do waveod, as capturas NAM (Clone) e os IRs de usuário ficam fora das seções
      gravadas e são mantidos. A seção Bank é regravada: os presets <b>originais</b> da lista voltam aos do arquivo.
      Faça um backup antes (Presets › Backup): ele guarda os dois tipos.</li>
    <li><b>Não desligue o pedal nem tire o cabo durante a gravação.</b></li>
  </ul>
  <label class="agree">
    <input type="checkbox" bind:checked={pedal.agree} />
    <span>Entendi: o pedal fica sem som durante a gravação, que leva alguns minutos.</span>
  </label>
  <footer>
    <button onclick={() => (pedal.fw = null)}>Cancelar</button>
    <button class="primary" disabled={!pedal.agree} onclick={() => pedal.update()}>Gravar {f.version} no pedal</button>
  </footer>
</div>

<style>
  .veil { position: fixed; inset: 0; z-index: 62; background: rgba(5, 5, 4, 0.62); }
  .sheet {
    position: fixed; z-index: 63; left: 50%; top: 45%; transform: translate(-50%, -50%); width: min(580px, calc(100vw - 48px));
    max-height: calc(100vh - 48px); overflow-y: auto;
    padding: 20px 22px 18px; border-radius: 14px; background: var(--raised); border: 1px solid var(--border-off); box-shadow: var(--float-shadow);
  }
  .sheet:focus { outline: none; }
  h3 { margin: 0 0 6px; font: 700 22px var(--f-display); }
  .file { display: flex; align-items: center; gap: 6px; margin: 0 0 12px; font: 500 13px var(--f-value); color: var(--label); }
  .secs { list-style: none; margin: 0 0 14px; padding: 0; display: grid; grid-template-columns: repeat(2, 1fr); gap: 4px 18px; }
  .secs li { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid var(--line); font-size: 13.5px; }
  .secs b { font: 500 13px var(--f-value); color: var(--label); }
  .notes { margin: 0 0 14px; padding-left: 18px; font-size: 13.5px; line-height: 1.55; color: var(--label); }
  .notes li { margin-bottom: 6px; }
  .notes b { color: #f6dcae; font-weight: 600; }
  .agree { display: flex; gap: 10px; align-items: flex-start; margin-bottom: 16px; font-size: 14px; cursor: pointer; }
  .agree input { margin-top: 3px; accent-color: var(--accent); }
  footer { display: flex; justify-content: flex-end; gap: 8px; }
  footer button { height: 36px; padding: 0 14px; border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 600 15px var(--f-display); }
  footer button:hover:not(:disabled) { background: var(--track); }
  footer button:disabled { opacity: 0.45; cursor: default; }
  .primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); font-weight: 700; }
  .primary:hover:not(:disabled) { background: #f7c169; }
</style>
