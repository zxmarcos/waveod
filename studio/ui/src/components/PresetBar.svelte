<script lang="ts">
  import Icon from './Icon.svelte';
  import { ctl } from '../lib/ctl.svelte';
  import { presetLabel, studio } from '../lib/store.svelte';
  import { t, type MsgKey, isAny } from '../lib/i18n.svelte';

  const rig = $derived(studio.rig);
  let listOpen = $state(false);
  let filter = $state('');
  let ask = $state<number | null>(null);   // a load waiting for "discard the edits?"

  const MODES: MsgKey[] = ['preset.route_series', 'preset.route_parallel', 'preset.route_dual'];

  function go(i: number) {
    listOpen = false;
    if (!rig || i === rig.index || i < 0 || i >= 240) return;
    if (rig.dirty) ask = i;
    else void studio.load(i);
  }
  async function resolve(how: 'discard' | 'save') {
    const i = ask;
    ask = null;
    if (i === null) return;
    if (how === 'save') await studio.save();
    await studio.load(i);
  }
  const shown = $derived(
    studio.presets.filter((p) => {
      const q = filter.trim().toLowerCase();
      return !q || p.name.toLowerCase().includes(q) || presetLabel(p.index).toLowerCase().includes(q);
    }));
  const isDefault = (name: string, n: number) => !name || isAny('ctl.scene', name.toUpperCase(), { n: n + 1 });

  // ---------------------------------------------------------------- the save sheet (the pedal's SAVE + NOME)
  let saving = $state(false);
  let saveName = $state('');
  let saveTo = $state(0);
  const dest = $derived(studio.presets[saveTo]);
  function openSave() {
    if (!rig) return;
    saveName = rig.name;
    saveTo = rig.index;
    saving = true;
  }
  async function doSave() {
    saving = false;
    await studio.save(saveTo, saveName);
  }
  function saveKey(e: KeyboardEvent) {
    if (e.key === 'Enter') { e.preventDefault(); void doSave(); }
    else if (e.key === 'Escape') { e.stopPropagation(); saving = false; }
  }

  // ---------------------------------------------------------------- the scene menu (the pedal's C1-C4 menu)
  let sceneMenu = $state(false);
  let renaming = $state(false);
  let sceneName = $state('');
  const act = $derived(rig?.scene.active ?? 0);
  function openScenes() {
    sceneMenu = !sceneMenu;
    renaming = false;
  }
  function startRename() {
    sceneName = rig?.scene.names[act] ?? '';
    renaming = true;
  }
  function takeFocus(node: HTMLInputElement) { node.focus(); node.select(); }
  function nameKey(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      sceneMenu = renaming = false;
      void studio.sceneRename(act, sceneName);
    } else if (e.key === 'Escape') {
      e.stopPropagation();
      renaming = false;
    }
  }
</script>

<div class="bar">
  <div class="preset">
    <button class="arrow" aria-label={t('bar.prev')} disabled={!rig || rig.index === 0}
            onclick={() => rig && go(rig.index - 1)}><Icon name="prev" size={20} stroke={2.4} /></button>
    <button class="arrow" aria-label={t('bar.next')} disabled={!rig || rig.index === 239}
            onclick={() => rig && go(rig.index + 1)}><Icon name="next" size={20} stroke={2.4} /></button>
    <button class="title" onclick={() => (listOpen = !listOpen)} aria-haspopup="listbox" aria-expanded={listOpen}>
      <span class="num">{rig ? presetLabel(rig.index) : '--'}</span>
      <span class="name">{rig?.name || t('ctl.unnamed')}</span>
      {#if rig?.dirty}<span class="dirty" title={t('bar.dirty')} aria-label={t('bar.dirty')}></span>{/if}
      <Icon name="down" size={18} stroke={2.2} />
    </button>
    <button class="save" class:idle={!rig?.dirty} disabled={!rig} onclick={openSave} aria-haspopup="dialog" aria-expanded={saving}
            title={rig ? t('bar.save_title', { label: presetLabel(rig.index) }) : ''}>
      <Icon name="save" size={17} stroke={2.1} /> {t('bar.save')}
    </button>
  </div>

  {#if saving && rig}
    <div class="scrim" role="presentation" onclick={() => (saving = false)}></div>
    <div class="savesheet" role="dialog" aria-label={t('bar.save_aria')}>
      <label>
        <span>{t('bar.name')}</span>
        <input class="name" bind:value={saveName} maxlength="17" spellcheck="false" use:takeFocus onkeydown={saveKey}
               placeholder={t('ctl.unnamed')} aria-label={t('preset.name_aria')} />
      </label>
      <label>
        <span>{t('bar.place')}</span>
        <select bind:value={saveTo} onkeydown={saveKey}>
          {#each studio.presets as p (p.index)}
            <option value={p.index}>{presetLabel(p.index)} · {p.index === rig.index ? t('bar.this') : p.place === 2 ? t('presets.vacant') : p.name || t('ctl.unnamed')}</option>
          {/each}
        </select>
      </label>
      <p class="hint">
        {#if saveTo !== rig.index && dest && dest.place !== 2}
          <b>{t('bar.replaces', { label: presetLabel(saveTo), name: dest.name || t('ctl.unnamed') })}</b>
        {:else if saveTo !== rig.index}
          {t('bar.to_empty', { label: presetLabel(rig.index) })}
        {:else}
          {t('preset.rename_hint')}
        {/if}
      </p>
      <div class="actions">
        <button class="primary" onclick={doSave}>{t('bar.save_to', { label: presetLabel(saveTo) })}</button>
        <button onclick={() => (saving = false)}>{t('common.cancel')}</button>
      </div>
    </div>
  {/if}

  <div class="scenewrap">
  <div class="group" role="radiogroup" aria-label={t('preset.scenes')}>
    {#each rig?.scene.names ?? ['', '', '', ''] as name, n (n)}
      <button role="radio" aria-checked={rig?.scene.active === n} class:on={rig?.scene.active === n}
              title={name || t('preset.scene_n', { n: n + 1 })} onclick={() => studio.scene(n)}>
        <b>C{n + 1}</b>{#if !isDefault(name, n)}<small>{name}</small>{/if}
      </button>
    {/each}
    {#if studio.canEditScenes}
      <button class="more" class:open={sceneMenu} onclick={openScenes} aria-haspopup="menu" aria-expanded={sceneMenu}
              title={t('bar.scene_menu_title', { n: act + 1 })} aria-label={t('bar.scene_menu_aria')}><Icon name="down" size={16} stroke={2.4} /></button>
    {/if}
  </div>

  {#if sceneMenu && rig}
    <div class="scrim" role="presentation" onclick={() => (sceneMenu = false)}></div>
    <div class="scenes" role="menu" aria-label={t('bar.scene_aria', { n: act + 1 })}>
      <p class="head"><b>C{act + 1}</b> {isDefault(rig.scene.names[act], act) ? '' : rig.scene.names[act]}
        <span>{rig.scene.editing ? t('bar.scene_editing') : t('bar.scene_same')}</span></p>
      <div class="copy">
        <span>{t('bar.copy_to')}</span>
        {#each [0, 1, 2, 3] as n (n)}
          {#if n !== act}<button role="menuitem" onclick={() => { sceneMenu = false; void studio.sceneCopy(act, n); }}>C{n + 1}</button>{/if}
        {/each}
      </div>
      {#if renaming}
        <input class="rename" bind:value={sceneName} maxlength="10" spellcheck="false" use:takeFocus onkeydown={nameKey}
               aria-label={t('bar.scene_name_aria')} placeholder={t('ctl.scene', { n: act + 1 })} />
        <p class="hint">{t('bar.scene_name_hint')}</p>
      {:else}
        <button role="menuitem" class="item" onclick={startRename}><Icon name="pencil" size={15} stroke={2} /> {t('bar.rename')}</button>
      {/if}
      <button role="menuitem" class="item" onclick={() => { sceneMenu = false; void studio.sceneRevert(act); }}
              title={t('bar.revert_title')}><Icon name="loop" size={15} stroke={2} /> {t('bar.revert')}</button>
    </div>
  {/if}
  </div>

  <div class="group" role="radiogroup" aria-label={t('preset.routing')}>
    {#each MODES as label, mode (mode)}
      <button role="radio" aria-checked={rig?.route.mode === mode} class:on={rig?.route.mode === mode}
              onclick={() => studio.route(mode)}>{t(label)}</button>
    {/each}
  </div>

  <button class="controls" disabled={!rig} onclick={() => rig && ctl.openPreset(rig.index, 'grid')}
          title={t('preset.controls_title')}>
    <Icon name="link" size={17} stroke={2.1} /> {t('ctl.title')}
  </button>

  <dl class="facts">
    <div><dt>BPM</dt><dd>{rig?.tempo ?? '--'}</dd></div>
    <div><dt>VOL</dt><dd>{rig?.volume ?? '--'}</dd></div>
  </dl>

  {#if listOpen}
    <div class="scrim" role="presentation" onclick={() => (listOpen = false)}></div>
    <div class="list" role="dialog" aria-label={t('tab.presets')}>
      <!-- svelte-ignore a11y_autofocus -->
      <input type="search" placeholder={t('bar.search')} bind:value={filter} autofocus />
      <ul role="listbox">
        {#each shown as p (p.index)}
          <li role="option" aria-selected={p.index === rig?.index}>
            <button class:current={p.index === rig?.index} class:vacant={p.place === 2} onclick={() => go(p.index)}>
              <span class="num">{presetLabel(p.index)}</span>
              <span>{p.place === 2 ? t('presets.vacant') : p.name || t('ctl.unnamed')}</span>
              {#if p.place === 1}<span class="orig" title={t('bar.original_title')}>{t('bar.original')}</span>{/if}
            </button>
          </li>
        {/each}
      </ul>
    </div>
  {/if}

  {#if ask !== null && rig}
    <div class="scrim" role="presentation" onclick={() => (ask = null)}></div>
    <div class="confirm" role="alertdialog" aria-labelledby="confirm-text">
      <p id="confirm-text">{t('bar.confirm', { label: presetLabel(rig.index), to: presetLabel(ask) })}</p>
      <div class="actions">
        <button class="primary" onclick={() => resolve('save')}>{t('bar.save_switch')}</button>
        <button onclick={() => resolve('discard')}>{t('bar.discard_changes')}</button>
        <button onclick={() => (ask = null)}>{t('common.cancel')}</button>
      </div>
    </div>
  {/if}
</div>

<style>
  .bar {
    position: relative;
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 14px 20px;
    border-bottom: 1px solid var(--line);
    flex-wrap: wrap;
  }
  .preset { display: flex; align-items: center; gap: 6px; flex: 1; min-width: 360px; }
  .arrow {
    width: 38px;
    height: 44px;
    display: grid;
    place-items: center;
    border-radius: var(--r-sm);
    background: var(--surface);
    border: 1px solid var(--line);
    color: var(--label);
  }
  .arrow:hover:not(:disabled) { color: var(--text); border-color: var(--border-off); }
  .arrow:disabled { opacity: 0.35; cursor: default; }
  .title {
    display: flex;
    align-items: center;
    gap: 12px;
    min-width: 0;
    height: 44px;
    padding: 0 12px 0 14px;
    border-radius: var(--r-sm);
    color: var(--label);
  }
  .title:hover { background: var(--surface); }
  .title .num { font: 500 20px var(--f-value); color: var(--accent); }
  .title .name {
    font: 700 28px/1 var(--f-display);
    color: var(--text);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .dirty {
    width: 9px;
    height: 9px;
    flex: none;
    border-radius: 50%;
    background: var(--accent);
    margin-left: -4px;
  }
  .save {
    display: flex;
    align-items: center;
    gap: 6px;
    height: 36px;
    padding: 0 14px;
    margin-left: 4px;
    border-radius: var(--r-sm);
    background: var(--accent);
    color: var(--accent-ink);
    font: 700 15px var(--f-display);
  }
  .save:hover:not(:disabled) { background: #f7c169; }
  .save.idle { background: var(--surface); color: var(--label); border: 1px solid var(--line); }
  .save.idle:hover:not(:disabled) { background: var(--raised); color: var(--text); }
  .save:disabled { background: var(--surface); color: var(--dim); border: 1px solid var(--line); cursor: default; }
  .savesheet {
    position: absolute; z-index: 31; top: calc(100% - 6px); left: 64px; width: 380px; padding: 14px;
    display: grid; gap: 10px;
    background: var(--raised); border: 1px solid var(--border-off); border-radius: var(--r-md); box-shadow: var(--float-shadow);
    animation: drop 0.22s var(--ease-out);
  }
  .savesheet label { display: grid; gap: 4px; }
  .savesheet label span { font: 600 12px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .savesheet input, .savesheet select {
    height: 36px; padding: 0 10px; border-radius: var(--r-sm); background: var(--bg); border: 1px solid var(--border-off);
    outline: none; color: var(--text);
  }
  .savesheet input { font: 700 18px var(--f-display); user-select: text; }
  .savesheet select { font: 500 14px var(--f-value); }
  .savesheet input:focus, .savesheet select:focus { border-color: var(--accent); }
  .savesheet .hint { margin: 0; font-size: 12.5px; line-height: 1.4; color: var(--label); }
  .savesheet .hint b { color: var(--accent); font-weight: 600; }

  .group {
    display: flex;
    padding: 3px;
    gap: 2px;
    border-radius: var(--r-md);
    background: var(--surface);
    border: 1px solid var(--line);
  }
  .group button {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-width: 46px;
    height: 36px;
    padding: 0 10px;
    border-radius: 7px;
    font: 600 15px/1 var(--f-display);
    color: var(--label);
  }
  .group button:hover:not(.on) { color: var(--text); background: var(--raised); }
  .group button.on { background: var(--accent); color: var(--accent-ink); }
  .group b { font-weight: 700; }
  .group small { font: 600 10.5px/1.1 var(--f-display); letter-spacing: 0.02em; max-width: 72px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .scenewrap { position: relative; }
  .group .more { min-width: 28px; padding: 0 6px; color: var(--label); }
  .group .more.open { background: var(--raised); color: var(--text); }
  .scenes {
    position: absolute; z-index: 31; top: calc(100% + 6px); right: 0; width: 290px; padding: 10px;
    background: var(--raised); border: 1px solid var(--border-off); border-radius: var(--r-md); box-shadow: var(--float-shadow);
    animation: drop 0.22s var(--ease-out);
  }
  .scenes .head { margin: 2px 4px 10px; font: 600 15px var(--f-display); }
  .scenes .head b { color: var(--accent); margin-right: 4px; }
  .scenes .head span { display: block; margin-top: 2px; font: 400 12px var(--f-body); color: var(--label); }
  .scenes .copy { display: flex; align-items: center; gap: 6px; margin: 0 4px 8px; }
  .scenes .copy span { flex: 1; font-size: 13px; color: var(--label); }
  .scenes .copy button { width: 40px; height: 30px; border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 700 14px var(--f-display); }
  .scenes .copy button:hover { background: var(--track); border-color: var(--accent); color: var(--accent); }
  .scenes .item { display: flex; align-items: center; gap: 8px; width: 100%; height: 34px; padding: 0 8px; border-radius: var(--r-sm); font: 600 14.5px var(--f-display); text-align: left; }
  .scenes .item:hover { background: var(--track); }
  .scenes .rename { width: 100%; height: 34px; padding: 0 8px; border-radius: var(--r-sm); background: var(--bg); border: 1px solid var(--accent); outline: none; font: 600 15px var(--f-display); text-transform: uppercase; user-select: text; }
  .scenes .hint { margin: 4px 4px 6px; font-size: 12px; color: var(--label); }
  .controls {
    display: flex;
    align-items: center;
    gap: 7px;
    height: 42px;
    padding: 0 14px;
    border-radius: var(--r-md);
    background: var(--surface);
    border: 1px solid var(--line);
    color: var(--label);
    font: 600 15px var(--f-display);
  }
  .controls:hover:not(:disabled) { color: var(--text); border-color: var(--border-off); }
  .controls:disabled { opacity: 0.35; cursor: default; }
  .facts { display: flex; gap: 14px; margin: 0; }
  @media (max-width: 1560px) { .facts { display: none; } }
  .facts div { display: flex; align-items: baseline; gap: 6px; }
  .facts dt { font: 600 12px var(--f-display); letter-spacing: 0.08em; color: var(--label); }
  .facts dd { margin: 0; font: 500 15px var(--f-value); color: var(--text); }

  .scrim { position: fixed; inset: 0; z-index: 30; }
  .list, .confirm {
    position: absolute;
    z-index: 31;
    top: calc(100% - 6px);
    left: 64px;
    background: var(--raised);
    border: 1px solid var(--border-off);
    border-radius: var(--r-md);
    box-shadow: var(--float-shadow);
    animation: drop 0.22s var(--ease-out);
  }
  @keyframes drop { from { opacity: 0; transform: translateY(-6px); } }
  .list { width: 380px; padding: 8px; }
  .list input {
    width: 100%;
    height: 34px;
    padding: 0 10px;
    border-radius: var(--r-sm);
    background: var(--bg);
    border: 1px solid var(--border-off);
    outline: none;
  }
  .list input:focus { border-color: var(--dim); }
  .list ul { list-style: none; margin: 8px 0 0; padding: 0; max-height: 460px; overflow-y: auto; }
  .list li button {
    display: flex;
    align-items: baseline;
    gap: 12px;
    width: 100%;
    padding: 6px 8px;
    border-radius: var(--r-sm);
    text-align: left;
    font: 600 16px var(--f-display);
  }
  .list li button:hover { background: var(--track); }
  .list li button.current { background: var(--bg); color: var(--accent); }
  .list li button.vacant { color: var(--off); font-weight: 500; }
  .list .num { font: 500 13px var(--f-value); color: var(--label); width: 34px; flex: none; }
  .orig { margin-left: auto; font: 600 11px var(--f-display); letter-spacing: 0.05em; color: var(--label); }
  .confirm { padding: 16px; width: 440px; }
  .confirm p { margin: 0 0 14px; font-size: 14px; line-height: 1.45; }
  .actions { display: flex; gap: 8px; }
  .actions button {
    height: 34px;
    padding: 0 12px;
    border-radius: var(--r-sm);
    border: 1px solid var(--border-off);
    font: 600 14px var(--f-display);
  }
  .actions button:hover { background: var(--track); }
  .actions .primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); }
  .actions .primary:hover { background: #f7c169; }
</style>
