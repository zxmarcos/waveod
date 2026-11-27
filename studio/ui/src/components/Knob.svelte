<script lang="ts">
  import type { Param } from '../lib/types';
  import { fraction } from '../lib/format';
  import { floating, moveFocus } from '../lib/floating';
  import { t } from '../lib/i18n.svelte';

  let { param, color, onchange, onreset, onhold, onscene }: {
    param: Param;
    color: string;
    onchange: (value: number) => void;
    onreset: () => void;
    onhold: (held: boolean) => void;
    onscene?: () => void;             /* the scene switch (KNOBS NA CENA): a value per scene, or the preset's */
  } = $props();

  const SIZE = 76, R = 29, C = SIZE / 2;
  const listed = $derived(param.options.length > 0);
  const frac = $derived(Math.min(1, Math.max(0, fraction(param))));
  const angle = $derived(-135 + 270 * frac);
  let open = $state(false);
  let held = $state(false);

  function pt(a: number, r = R) {
    const rad = (a * Math.PI) / 180;
    return [C + r * Math.sin(rad), C - r * Math.cos(rad)];
  }
  function arc(a0: number, a1: number) {
    const [x0, y0] = pt(a0), [x1, y1] = pt(a1);
    return `M${x0} ${y0}A${R} ${R} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  }

  const unit = $derived(listed ? 1 : param.step > 0 ? param.step : (param.max - param.min) / 100);

  let startY = 0, startV = 0;
  function down(e: PointerEvent) {
    if (e.button !== 0) return;
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    startY = e.clientY;
    startV = param.value;
    held = true;
    onhold(true);
  }
  function move(e: PointerEvent) {
    if (!held) return;
    const dy = startY - e.clientY;
    const span = param.max - param.min;
    const v = listed ? startV + Math.round(dy / 14) : startV + (dy / (e.shiftKey ? 900 : 180)) * span;
    onchange(v);
  }
  function up(e: PointerEvent) {
    if (!held) return;
    (e.currentTarget as Element).releasePointerCapture(e.pointerId);
    held = false;
    onhold(false);
  }
  /** one notch: a step, or a hundredth of the range when the steps are tiny (Shift: always a step) */
  function wheel(e: WheelEvent) {
    e.preventDefault();
    const span = param.max - param.min;
    const coarse = !listed && span / unit > 200 ? span / 100 : unit;
    onchange(param.value + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? unit : coarse));
  }
  function key(e: KeyboardEvent) {
    const d = e.key === 'ArrowUp' || e.key === 'ArrowRight' ? 1 : e.key === 'ArrowDown' || e.key === 'ArrowLeft' ? -1 : 0;
    if (d) {
      e.preventDefault();
      onchange(param.value + d * unit * (e.shiftKey ? 10 : 1));
    }
  }
  let anchor = $state<HTMLButtonElement>();
  function pick(i: number) {
    onchange(i);
    close();
  }
  function close(refocus = false) {
    open = false;
    if (refocus) anchor?.focus();
  }
  /** the list opens on the chosen option */
  function focusCurrent(list: HTMLElement) {
    const cur = list.querySelector<HTMLElement>('button.current') ?? list.querySelector<HTMLElement>('button');
    cur?.focus({ preventScroll: true });
    cur?.scrollIntoView({ block: 'center' });
  }
  function listKey(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      e.stopPropagation();                  // not the window's Escape (which drops the selection)
      close(true);
    } else if (e.key === 'Tab') {
      close();
    } else {
      moveFocus(e.currentTarget as HTMLElement, e);
    }
  }
</script>

<div class="knob" class:held>
  <span class="name" title={param.name}>{param.name}</span>
  <div class="dial" role="slider" tabindex="0" aria-label={param.name} aria-valuetext={param.text}
       aria-valuemin={param.min} aria-valuemax={param.max} aria-valuenow={param.value}
       onpointerdown={down} onpointermove={move} onpointerup={up} onpointercancel={up}
       ondblclick={onreset} onwheel={wheel} onkeydown={key}>
    <svg width={SIZE} height={SIZE} viewBox="0 0 {SIZE} {SIZE}">
      <path d={arc(-135, 135)} class="track" />
      {#if frac > 0.002}<path d={arc(-135, angle)} stroke={color} class="value" />{/if}
      <circle cx={C} cy={C} r={R - 8} class="cap" />
      <line x1={pt(angle, R - 18)[0]} y1={pt(angle, R - 18)[1]} x2={pt(angle, R - 9)[0]} y2={pt(angle, R - 9)[1]}
            class="pointer" />
      {#if param.scene && !onscene}<circle cx={C} cy={SIZE - 6} r="2.5" fill="var(--accent)"><title>{t('params.scene_param')}</title></circle>{/if}
    </svg>
    {#if onscene}
      <button class="scene" class:on={param.scene} aria-pressed={param.scene} onclick={onscene}
              onpointerdown={(e) => e.stopPropagation()}
              title={param.scene ? t('params.scene_on_title') : t('params.scene_off_title')}>C</button>
    {/if}
  </div>
  {#if listed}
    <button class="text listed" bind:this={anchor} onclick={() => (open = !open)} aria-haspopup="listbox"
            aria-expanded={open} title={param.text}>
      {param.text}
    </button>
    {#if open && anchor}
      <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
      <ul class="options" role="listbox" aria-label={param.name} onkeydown={listKey}
          use:floating={{ anchor, onclose: () => close() }} use:focusCurrent>
        {#each param.options as o, i (i)}
          {#if o}
            <li role="option" aria-selected={Math.round(param.value) === i}>
              <button class:current={Math.round(param.value) === i} onclick={() => pick(i)}
                      style:--c={color}>{o}</button>
            </li>
          {/if}
        {/each}
      </ul>
    {/if}
  {:else}
    <span class="text">{param.text}</span>
  {/if}
</div>

<style>
  .dial { position: relative; }
  .scene {
    position: absolute; top: 0; right: 0; width: 18px; height: 18px; border-radius: 5px;
    font: 700 11px/18px var(--f-display); text-align: center; color: var(--off);
    border: 1px solid var(--border-off); background: var(--bg); opacity: 0; transition: opacity 0.15s;
  }
  .knob:hover .scene, .scene:focus-visible { opacity: 1; }
  .scene:hover { color: var(--text); border-color: var(--label); }
  .scene.on { opacity: 1; color: var(--accent-ink); background: var(--accent); border-color: var(--accent); }
  .knob {
    position: relative;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 4px;
    width: 104px;
    flex: none;
  }
  .name {
    font: 600 13px/1.2 var(--f-display);
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--label);
    max-width: 100%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .held .name { color: var(--text); }
  .dial { cursor: ns-resize; border-radius: 50%; touch-action: none; }
  .track { fill: none; stroke: var(--track); stroke-width: 5; stroke-linecap: round; }
  .value { fill: none; stroke-width: 5; stroke-linecap: round; }
  .cap { fill: var(--raised); stroke: var(--border-off); stroke-width: 1; }
  .pointer { stroke: var(--text); stroke-width: 2.5; stroke-linecap: round; }
  .text {
    font: 500 13px/1.3 var(--f-value);
    color: var(--text);
    max-width: 100%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    padding: 2px 6px;
    border-radius: var(--r-sm);
  }
  .listed { border: 1px solid var(--border-off); }
  .listed:hover { border-color: var(--dim); background: var(--raised); }
  /* placed by floating() (lib/floating.ts), in <body>: the knob row clips what sits inside it */
  .options {
    position: fixed;
    z-index: 40;
    list-style: none;
    margin: 0;
    padding: 6px;
    min-width: 170px;
    max-width: 300px;
    overflow-y: auto;
    overscroll-behavior: contain;
    animation: pop 0.16s var(--ease-out);
    background: var(--raised);
    border: 1px solid var(--border-off);
    border-radius: var(--r-md);
    box-shadow: var(--float-shadow);
  }
  .options button {
    display: block;
    width: 100%;
    text-align: left;
    padding: 6px 10px;
    border-radius: var(--r-sm);
    font: 500 14px/1.3 var(--f-display);
    color: var(--text);
  }
  .options button:hover, .options button:focus-visible { background: var(--track); outline: none; }
  .options button.current { color: var(--c); background: var(--bg); }
  .options button.current:focus-visible { box-shadow: inset 0 0 0 1px var(--c); }
  @keyframes pop { from { opacity: 0; } }
  @media (prefers-reduced-motion: reduce) { .options { animation: none; } }
</style>
