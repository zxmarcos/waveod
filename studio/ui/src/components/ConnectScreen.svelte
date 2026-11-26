<!--
  Waiting for the pedal: what is missing in words a player acts on (switch it on, plug the cable, close the other
  program, update the firmware), and the Studio keeps looking by itself; nobody has to close and open the app.
  Full window before the first connection; over the dimmed screens when the pedal goes away later.
-->
<script lang="ts">
  import Icon from './Icon.svelte';
  import { studio } from '../lib/store.svelte';
  import { pedal } from '../lib/pedal.svelte';

  let { overlay = false }: { overlay?: boolean } = $props();

  const s = $derived(studio.link);
  const phase = $derived(s?.phase ?? 'starting');
  const loading = $derived(phase === 'connected');           // found; its data on the way
  const emu = $derived(s?.transport === 'emulador');
  const tone = $derived(phase === 'old' || phase === 'failed' ? 'bad' : phase === 'busy' || phase === 'silent' ? 'warn' : 'look');

  const TITLES: Record<string, string> = {
    starting: 'Procurando o pedal…',
    searching: 'Esperando o pedal',
    busy: 'A porta do pedal está em uso',
    silent: 'O pedal não respondeu',
    old: 'Atualize o waveod do pedal',
    failed: 'O emulador não iniciou',
    connected: 'Lendo o pedal…',
    updating: 'Gravando o firmware…',
  };
  const title = $derived(overlay && phase === 'searching' ? 'Pedal desconectado' : TITLES[phase] ?? 'Esperando o pedal');
  let retrying = $state(false);
  async function retry() {
    retrying = true;
    await studio.retryLink();
    retrying = false;
  }
</script>

<div class="wait" class:overlay role="status" aria-live="polite">
  <div class="card">
    <div class="beacon {tone}" class:still={tone === 'bad'} aria-hidden="true">
      <span class="ring"></span><span class="ring two"></span>
      <span class="core"><Icon name="pedal" size={34} stroke={1.8} /></span>
    </div>
    <h1>{title}</h1>

    {#if loading}
      <p class="lead">{s?.name || 'Pedal'} encontrado · waveod {s?.version}. Lendo o preset, a lista e os efeitos.</p>
    {:else if phase === 'starting' && emu}
      <p class="lead">O firmware está iniciando no emulador; leva alguns segundos.</p>
    {:else if phase === 'searching' || phase === 'starting'}
      <p class="lead">
        {overlay ? 'O Studio parou de ver o pedal. Assim que ele voltar, tudo é lido de novo; as telas continuam aqui.'
                 : 'O Studio conecta sozinho assim que o pedal aparecer. Não precisa fechar o programa.'}
      </p>
      <ol class="steps">
        <li><b>Ligue o pedal</b> na fonte e espere a tela inicial.</li>
        <li><b>Conecte o cabo USB</b> no computador: um cabo de dados, não só de carga.</li>
        <li><b>Feche outros programas</b> que usam o pedal (o editor original, o Studio clássico).</li>
      </ol>
    {:else if phase === 'busy'}
      <p class="lead">O pedal está conectado ({s?.name}), mas outro programa está com a porta MIDI dele aberta. Só um
        programa por vez pode usá-la.</p>
      <ol class="steps">
        <li><b>Feche o editor original</b> do pedal, ou o Studio clássico (waveod-studio.bat).</li>
        <li>O Studio conecta sozinho quando a porta ficar livre.</li>
      </ol>
    {:else if phase === 'silent'}
      <p class="lead">A porta {s?.name} abriu, mas o pedal não respondeu aos pedidos do waveod.</p>
      <ol class="steps">
        <li>Se você <b>acabou de ligar</b> o pedal, espere ele terminar de iniciar.</li>
        <li>Se ele está com <b>outro firmware</b> (o original) ou parado no <b>modo de atualização</b>, grave o waveod.</li>
      </ol>
      <button class="fw" onclick={() => pedal.pickFirmware()}><Icon name="file" size={15} stroke={2} /> Gravar firmware…</button>
    {:else if phase === 'old'}
      <p class="lead">O pedal tem o waveod {s?.version} (protocolo {s?.protocol}). Esta interface precisa do protocolo 8
        ou mais novo.</p>
      <ol class="steps">
        <li>Grave um waveod novo: um .bin feito com o protocolo 8 ou mais novo.</li>
        <li>Quando o pedal reiniciar com ele, o Studio conecta sozinho.</li>
      </ol>
      <button class="fw" onclick={() => pedal.pickFirmware()}><Icon name="file" size={15} stroke={2} /> Gravar firmware…</button>
    {:else if phase === 'updating'}
      <p class="lead">Não desligue o pedal nem tire o cabo. Quando terminar, ele reinicia e o Studio conecta sozinho.</p>
    {:else if phase === 'failed'}
      <p class="lead">{s?.message}</p>
    {/if}

    {#if !loading && phase !== 'failed' && phase !== 'updating'}
      <div class="foot">
        <span class="mono">
          {s?.message && phase === 'searching' && s.message !== 'Nenhum pedal conectado.' ? s.message + ' · ' : ''}tentativa {s?.attempts ?? 0}
        </span>
        <button onclick={retry} disabled={retrying || (phase === 'starting' && emu)}>Tentar agora</button>
      </div>
    {/if}
  </div>
</div>

<style>
  .wait {
    flex: 1;
    display: grid;
    place-items: center;
    padding: 24px;
    min-height: 0;
    overflow-y: auto;
  }
  .wait.overlay {
    position: fixed;
    inset: 52px 0 0;
    z-index: 55;
    background: rgba(10, 10, 9, 0.78);
    backdrop-filter: blur(3px);
    animation: fade 0.25s;
  }
  .card { width: min(560px, 100%); text-align: center; }
  .overlay .card {
    padding: 28px 32px 22px;
    border-radius: 16px;
    background: var(--raised);
    border: 1px solid var(--border-off);
    box-shadow: var(--float-shadow);
  }
  .beacon { position: relative; width: 112px; height: 112px; margin: 0 auto 22px; --c: var(--accent); }
  .beacon.warn { --c: #f6c15a; }
  .beacon.bad { --c: var(--error); }
  .core {
    position: absolute;
    inset: 22px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    border: 2px solid var(--c);
    color: var(--c);
    background: var(--bg);
  }
  .ring {
    position: absolute;
    inset: 22px;
    border-radius: 50%;
    border: 2px solid var(--c);
    opacity: 0;
    animation: ping 2.4s var(--ease-out) infinite;
  }
  .ring.two { animation-delay: 1.2s; }
  .still .ring { display: none; }
  @keyframes ping { 0% { transform: scale(1); opacity: 0.55; } 100% { transform: scale(1.9); opacity: 0; } }
  @media (prefers-reduced-motion: reduce) { .ring { animation: none; } }
  h1 { margin: 0 0 10px; font: 700 30px/1.15 var(--f-display); }
  .lead { margin: 0 auto 18px; max-width: 480px; color: var(--label); font-size: 14.5px; line-height: 1.55; }
  .steps {
    list-style: none;
    counter-reset: s;
    margin: 0 auto 22px;
    padding: 0;
    max-width: 440px;
    text-align: left;
  }
  .steps li {
    counter-increment: s;
    position: relative;
    padding: 9px 0 9px 40px;
    border-top: 1px solid var(--line);
    font-size: 14px;
    line-height: 1.45;
  }
  .steps li:last-child { border-bottom: 1px solid var(--line); }
  .steps li::before {
    content: counter(s);
    position: absolute;
    left: 6px;
    top: 9px;
    width: 22px;
    height: 22px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    background: var(--surface);
    border: 1px solid var(--border-off);
    font: 500 12px var(--f-value);
    color: var(--label);
  }
  .steps b { font-weight: 500; color: var(--text); }
  .fw {
    display: inline-flex; align-items: center; gap: 7px; height: 36px; padding: 0 14px; margin: 0 auto 20px;
    border-radius: var(--r-sm); background: var(--accent); color: var(--accent-ink); font: 700 15px var(--f-display);
  }
  .fw:hover { background: #f7c169; }
  .foot { display: flex; align-items: center; justify-content: center; gap: 14px; }
  .mono { font: 500 12px var(--f-value); color: var(--dim); }
  .foot button {
    height: 34px;
    padding: 0 14px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 14.5px var(--f-display);
  }
  .foot button:hover:not(:disabled) { background: var(--track); }
  .foot button:disabled { opacity: 0.5; cursor: default; }
  @keyframes fade { from { opacity: 0; } }
</style>
