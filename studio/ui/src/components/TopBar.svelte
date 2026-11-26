<script lang="ts">
  import mark from '../assets/mark.png';
  import { studio, type Screen } from '../lib/store.svelte';

  // the screens still in the classic app (python -m waveod_studio) while this one grows
  const TABS: { id: Screen; label: string; ready: boolean }[] = [
    { id: 'grid', label: 'Grade', ready: true },
    { id: 'presets', label: 'Presets', ready: true },
    { id: 'effects', label: 'Efeitos', ready: true },
    { id: 'nam', label: 'NAM', ready: true },
    { id: 'irs', label: 'IRs', ready: true },
    { id: 'drums', label: 'Bateria', ready: true },
    { id: 'global', label: 'Global', ready: true },
    { id: 'perf', label: 'Desempenho', ready: true },
    { id: 'pedal', label: 'Pedal', ready: true },
  ];
  const l = $derived(studio.link);
  const PHASES: Record<string, string> = {
    starting: 'procurando o pedal…', searching: 'esperando o pedal', busy: 'porta em uso por outro programa',
    silent: 'o pedal não respondeu', old: 'firmware antigo', failed: 'emulador parado', updating: 'gravando firmware…',
  };
  const where = $derived(
    !l ? 'iniciando…' : l.phase !== 'connected' ? PHASES[l.phase] ?? l.phase :
    l.transport === 'usb' ? `${l.name} · ${l.version}` : `${l.transport} · ${l.version}`);
  const tone = $derived(!l || l.phase === 'starting' || l.phase === 'searching' ? 'look' :
                         l.phase === 'connected' ? 'ok' : 'bad');
</script>

<header class="top">
  <div class="brand">
    <img src={mark} alt="" width="52" height="20" />
    <span class="word">waveod <b>Studio</b></span>
  </div>
  <nav aria-label="Telas">
    {#each TABS as t (t.id)}
      <button class:current={t.id === studio.screen} disabled={!t.ready || !studio.everConnected}
              aria-current={t.id === studio.screen ? 'page' : undefined}
              onclick={() => t.ready && studio.show(t.id as Screen)}
              title={t.ready ? '' : 'Por enquanto no app clássico (waveod-studio.bat)'}>{t.label}</button>
    {/each}
  </nav>
  <div class="link" class:ok={tone === 'ok'} class:bad={tone === 'bad'} class:look={tone === 'look'}>
    <span class="dot"></span>{where}
  </div>
</header>

<style>
  .top {
    display: flex;
    align-items: center;
    gap: 28px;
    height: 52px;
    flex: none;
    padding: 0 20px;
    border-bottom: 1px solid var(--line);
    background: var(--bg);
  }
  .brand { display: flex; align-items: center; gap: 10px; width: 262px; flex: none; }
  @media (max-width: 1260px) { .brand { width: auto; } nav button { padding: 0 11px; } }
  .word { font: 600 19px/1 var(--f-display); color: var(--text); letter-spacing: 0.01em; }
  .word b { font-weight: 600; color: var(--label); }
  nav { display: flex; gap: 4px; flex: 1; min-width: 0; }
  nav button {
    position: relative;
    height: 52px;
    padding: 0 14px;
    font: 600 15px/1 var(--f-display);
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--label);
  }
  nav button:disabled { color: var(--dim); cursor: default; }
  nav button.current { color: var(--text); }
  nav button.current::after {
    content: '';
    position: absolute;
    left: 14px;
    right: 14px;
    bottom: -1px;
    height: 2px;
    background: var(--accent);
  }
  .link {
    display: flex;
    align-items: center;
    gap: 8px;
    font: 500 12.5px var(--f-value);
    color: var(--label);
    white-space: nowrap;
    flex: none;
  }
  .dot { flex: none; width: 8px; height: 8px; border-radius: 50%; background: var(--dim); }
  .ok .dot { background: var(--ok); }
  .bad .dot { background: var(--error); }
  .look .dot { background: var(--accent); animation: blink 1.2s ease-in-out infinite; }
  @keyframes blink { 50% { opacity: 0.25; } }
  @media (prefers-reduced-motion: reduce) { .look .dot { animation: none; } }
</style>
