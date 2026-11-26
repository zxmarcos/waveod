import { mount } from 'svelte';
import './app.css';
import App from './App.svelte';
import { studio } from './lib/store.svelte';
import { drums } from './lib/drums.svelte';
import { effects } from './lib/fx.svelte';
import { nam } from './lib/nam.svelte';
import { pedal } from './lib/pedal.svelte';
import { ctl } from './lib/ctl.svelte';
import { globals } from './lib/globals.svelte';
import { irs } from './lib/irs.svelte';
import * as irmath from './lib/irmath';
import * as cabmic from './lib/cabmic';
import * as globalset from './lib/globalset';
import * as controls from './lib/controls';

// the state, for the developer tools (--debug) and the window tests
Object.assign(window, { waveod: studio, waveodDrums: drums, waveodFx: effects, waveodNam: nam, waveodPedal: pedal,
                      waveodCtl: ctl, waveodControls: controls, waveodGlobals: globals,
                      waveodGlobalset: globalset, waveodIrs: irs, waveodIrmath: irmath,
                      waveodCabmic: cabmic });

export default mount(App, { target: document.getElementById('app')! });
