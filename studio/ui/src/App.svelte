<!--
  THESIS: the chain is the screen. The pedal's own 2 x 6 grid, wired the way the signal really flows, at a size
  you can grab; not a list of slots with a form beside it.
  OWN-WORLD: the QME-200 display grown up: near-black, amber for the app's own actions, every effect in its
  menu colour with the pedal's line icons; Barlow Semi Condensed names, Plex Mono readouts.
  STORY: open it, see the preset as the pedal plays it, pick a block, turn its knobs, drag models in, switch
  routing and scenes; the pedal answers every move.
  FIRST VIEWPORT: browser rail + list on the left (312px); preset bar on top of the stage; the grid centred
  and scaled to fit; the selected block's knobs in a 236px panel at the bottom.
  FORM: Cortex-style editor layout (user's choice), pedal identity (DESIGN.md).
-->
<script lang="ts">
  import TopBar from './components/TopBar.svelte';
  import Browser from './components/Browser.svelte';
  import PresetBar from './components/PresetBar.svelte';
  import Grid from './components/Grid.svelte';
  import ParamPanel from './components/ParamPanel.svelte';
  import Toast from './components/Toast.svelte';
  import Dialog from './components/Dialog.svelte';
  import PresetsScreen from './components/PresetsScreen.svelte';
  import DrumsScreen from './components/DrumsScreen.svelte';
  import ConnectScreen from './components/ConnectScreen.svelte';
  import PedalScreen from './components/PedalScreen.svelte';
  import EffectsScreen from './components/EffectsScreen.svelte';
  import NamScreen from './components/NamScreen.svelte';
  import GlobalScreen from './components/GlobalScreen.svelte';
  import IrsScreen from './components/IrsScreen.svelte';
  import PerfScreen from './components/PerfScreen.svelte';
  import FirmwareSheet from './components/FirmwareSheet.svelte';
  import ControlsEditor from './components/ControlsEditor.svelte';
  import { ctl } from './lib/ctl.svelte';
  import { pedal } from './lib/pedal.svelte';
  import { studio } from './lib/store.svelte';

  $effect(() => { void studio.start(); });

  function key(e: KeyboardEvent) {
    const t = e.target as HTMLElement;
    if (t.tagName === 'INPUT' || studio.dialog || studio.progress || !studio.online) return;
    if (ctl.showsOn(studio.screen)) return;                  // the controls editor has its own keys
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
      e.preventDefault();
      if (studio.rig?.dirty) void studio.save();
      return;
    }
    if (studio.screen !== 'grid') return;
    const blk = studio.selectedBlock;
    if (e.key === 'Delete' && blk) void studio.remove(blk.slot);
    else if (e.key === 'Escape') studio.selection = null;
  }
</script>

<svelte:window onkeydown={key} />

<div class="app">
  <TopBar />
  <div class="body">
    {#if !studio.everConnected}
      <ConnectScreen />
    {:else if ctl.showsOn(studio.screen)}
      <ControlsEditor />
    {:else if studio.screen === 'presets'}
      <PresetsScreen />
    {:else if studio.screen === 'drums'}
      <DrumsScreen />
    {:else if studio.screen === 'pedal'}
      <PedalScreen />
    {:else if studio.screen === 'effects'}
      <EffectsScreen />
    {:else if studio.screen === 'nam'}
      <NamScreen />
    {:else if studio.screen === 'global'}
      <GlobalScreen />
    {:else if studio.screen === 'irs'}
      <IrsScreen />
    {:else if studio.screen === 'perf'}
      <PerfScreen />
    {:else}
      <Browser />
      <main>
        <PresetBar />
        <Grid />
        <ParamPanel />
      </main>
    {/if}
  </div>
  {#if studio.everConnected && !studio.online && studio.link?.phase !== 'updating'}<ConnectScreen overlay />{/if}
  {#if pedal.fw}<FirmwareSheet />{/if}
  <Toast />
  <Dialog />
</div>

<style>
  .app { display: flex; flex-direction: column; height: 100%; min-width: 1100px; }
  .body { flex: 1; display: flex; min-height: 0; }
  main { flex: 1; min-width: 0; display: flex; flex-direction: column; }
</style>
