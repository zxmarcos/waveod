<!--
  waveod's own effects on the pedal: the modules installed in its library, each with what it asks of the pedal
  (memory, the knobs it brings), and module files coming in after a look at what they are.
-->
<script lang="ts">
  import { untrack } from 'svelte';
  import Icon from './Icon.svelte';
  import { menuInfo } from '../lib/menus';
  import { effects, fxValue, kb } from '../lib/fx.svelte';
  import { t, tn } from '../lib/i18n.svelte';

  $effect(() => { untrack(() => void effects.enter()); });

  const lib = $derived(effects.lib);
  const m = $derived(effects.module);
  const groups = $derived.by(() => {
    const by = new Map<number, typeof lib extends null ? never : NonNullable<typeof lib>['modules']>();
    for (const x of [...(lib?.modules ?? [])].sort((a, b) => a.menu - b.menu || a.name.localeCompare(b.name))) {
      if (!by.has(x.menu)) by.set(x.menu, []);
      by.get(x.menu)!.push(x);
    }
    return [...by.entries()];
  });
  const usedPct = $derived(lib && lib.capacity ? (100 * lib.used) / lib.capacity : 0);
  const good = $derived((effects.picks ?? []).filter((p) => !p.error));
  const range = (unit: string, a: number, b: number) => `${fxValue(unit, a)} … ${fxValue(unit, b)}`;
  function take(node: HTMLElement) { node.querySelector<HTMLElement>('footer .primary')?.focus() ?? node.focus(); }
  function key(e: KeyboardEvent) {
    if (e.key === 'Escape' && effects.picks) {
      e.stopPropagation();
      effects.picks = null;
    }
  }
</script>

<div class="screen">
  <header class="tools">
    <div class="title">
      <h1>{t('tab.effects')}</h1>
      <span class="sub">{t('fx.sub')}</span>
    </div>
    {#if lib}
      {@const usage = t('fx.usage', { used: kb(lib.used), total: kb(lib.capacity) })}
      <div class="usage" title={usage}>
        <div class="meter"><span style:width="{Math.max(usedPct, lib.used ? 1 : 0)}%"></span></div>
        <span>{usage}</span>
      </div>
    {/if}
    <button class="primary" onclick={() => effects.pick()}><Icon name="file" size={16} stroke={2.1} /> {t('fx.install')}</button>
  </header>

  {#if !lib}
    <p class="note">{t('fx.loading')}</p>
  {:else if !lib.modules.length}
    <div class="empty">
      <h2>{t('fx.empty_title')}</h2>
      <p>{t('fx.empty_text')}</p>
      <button class="primary" onclick={() => effects.pick()}><Icon name="file" size={16} stroke={2.1} /> {t('fx.install')}</button>
    </div>
  {:else}
    <div class="body">
      <nav class="list" aria-label={t('fx.list_aria')}>
        {#each groups as [menu, mods] (menu)}
          {@const mi = menuInfo(menu)}
          <h3 style:--c={mi.color}>{mi.name}</h3>
          {#each mods as x (x.id)}
            <button class:picked={x.id === effects.picked} style:--c={mi.color} onclick={() => (effects.picked = x.id)}
                    aria-current={x.id === effects.picked ? 'true' : undefined}>
              <Icon name={mi.icon} size={17} stroke={2} />
              <span class="nm">{x.name}</span>
              <span class="sz">{kb(x.size)}</span>
            </button>
          {/each}
        {/each}
      </nav>

      {#if m}
        {@const mi = menuInfo(m.menu)}
        <section class="detail" style:--c={mi.color} aria-label={m.name}>
          <div class="head">
            <span class="badge"><Icon name={mi.icon} size={24} stroke={2} /></span>
            <div>
              <span class="menu">{mi.name}{m.category ? ` · ${m.category}` : ''}</span>
              <h2>{m.name}</h2>
            </div>
            <button class="danger" onclick={() => effects.remove(m)}><Icon name="trash" size={15} stroke={2} /> {t('common.remove')}</button>
          </div>
          <dl class="facts">
            <div><dt>{t('fx.fact_id')}</dt><dd>{m.hex}</dd></div>
            <div><dt>{t('fx.fact_module')}</dt><dd>{kb(m.size)}</dd></div>
            <div><dt>{t('fx.fact_memory')}</dt><dd>{m.buffer ? t('fx.memory_buffer', { state: kb(m.state), buffer: kb(m.buffer) }) : kb(m.state)}</dd></div>
            <div><dt>{t('fx.fact_knobs')}</dt><dd>{m.params.length}</dd></div>
          </dl>
          <p class="where">{t('fx.where', { menu: mi.name })}</p>
          {#if m.params.length}
            <table>
              <thead><tr><th>{t('fx.col_param')}</th><th>{t('fx.col_range')}</th><th>{t('fx.col_default')}</th><th>{t('fx.col_values')}</th></tr></thead>
              <tbody>
                {#each m.params as q, k (k)}
                  <tr>
                    <td class="pn">{q.name}{#if q.sync}<span class="tag">sync</span>{/if}</td>
                    {#if q.options.length}
                      <td class="mono">{tn('fx.options', q.options.length)}</td>
                      <td class="mono">{q.options[Math.round(q.default)] ?? fxValue('', q.default)}</td>
                      <td class="opts">{#each q.options.filter(Boolean) as o (o)}<span>{o}</span>{/each}</td>
                    {:else}
                      <td class="mono">{range(q.unit, q.min, q.max)}</td>
                      <td class="mono">{fxValue(q.unit, q.default)}</td>
                      <td class="mono dim">{t('fx.step', { v: fxValue(q.unit.replace(/^[^%]*/, ''), q.step) })}</td>
                    {/if}
                  </tr>
                {/each}
              </tbody>
            </table>
          {/if}
        </section>
      {/if}
    </div>
  {/if}
</div>

{#if effects.picks}
  <div class="veil" role="presentation" onclick={() => (effects.picks = null)}></div>
  <div class="sheet" role="dialog" aria-modal="true" aria-labelledby="fx-title" tabindex="-1" onkeydown={key} use:take>
    <h3 id="fx-title">{t('fx.confirm_title')}</h3>
    <ul>
      {#each effects.picks as p (p.path)}
        {@const mi = p.menu !== undefined ? menuInfo(p.menu) : null}
        {@const old = effects.installed(p.id)}
        <li class:bad={!!p.error} style:--c={mi?.color ?? 'var(--error)'}>
          <span class="ic">{#if mi}<Icon name={mi.icon} size={18} stroke={2} />{:else}<Icon name="close" size={18} stroke={2} />{/if}</span>
          <div class="txt">
            <b>{p.error ? p.file : p.name}</b>
            <small>{p.error ? p.error : t('fx.pick_info', { menu: `${mi?.name}${p.category ? ` · ${p.category}` : ''}`,
                                                            knobs: p.params?.length ?? 0, size: kb(p.size ?? 0), file: p.file })}</small>
          </div>
          {#if !p.error}<span class="state" class:replace={!!old}>{old ? t('fx.replaces') : t('fx.new')}</span>{/if}
        </li>
      {/each}
    </ul>
    <p class="hint">{t('fx.hint')} {#if good.length < (effects.picks?.length ?? 0)}{t('fx.hint_errors')}{/if}</p>
    <footer>
      <button onclick={() => (effects.picks = null)}>{t('common.cancel')}</button>
      <button class="primary" disabled={!good.length} onclick={() => effects.install()}>
        {tn('fx.install_n', good.length)}
      </button>
    </footer>
  </div>
{/if}

<style>
  .screen { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .tools { display: flex; align-items: center; gap: 18px; padding: 16px 24px 14px; border-bottom: 1px solid var(--line); }
  .title { display: flex; align-items: baseline; gap: 12px; flex: 1; min-width: 0; }
  h1 { margin: 0; font: 700 28px/1 var(--f-display); }
  .sub { font-size: 13px; color: var(--label); white-space: nowrap; }
  .usage { display: flex; align-items: center; gap: 10px; font: 500 12.5px var(--f-value); color: var(--label); white-space: nowrap; }
  .meter { width: 140px; height: 6px; border-radius: 3px; background: var(--track); overflow: hidden; }
  .meter span { display: block; height: 100%; background: var(--accent); }
  .primary {
    display: flex; align-items: center; gap: 7px; height: 36px; padding: 0 14px; border-radius: var(--r-sm);
    background: var(--accent); color: var(--accent-ink); font: 700 15px var(--f-display); border: 1px solid var(--accent);
  }
  .primary:hover:not(:disabled) { background: #f7c169; }
  .primary:disabled { opacity: 0.5; cursor: default; }
  .note { margin: 24px; color: var(--label); }
  .empty { max-width: 560px; margin: 60px auto; text-align: center; }
  .empty h2 { margin: 0 0 10px; font: 700 28px var(--f-display); }
  .empty p { color: var(--label); line-height: 1.55; margin: 0 0 20px; }
  .empty .primary { margin: 0 auto; }
  .body { flex: 1; min-height: 0; display: flex; }
  .list { flex: none; width: 300px; overflow-y: auto; padding: 8px 10px 20px; border-right: 1px solid var(--line); background: var(--surface); }
  .list h3 {
    display: flex; align-items: center; gap: 8px; margin: 14px 8px 6px;
    font: 600 12px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--c);
  }
  .list button {
    display: flex; align-items: center; gap: 10px; width: 100%; height: 38px; padding: 0 10px;
    border-radius: var(--r-sm); color: var(--c); text-align: left;
  }
  .list button:hover { background: var(--raised); }
  .list button.picked { background: var(--raised); box-shadow: inset 0 0 0 1px var(--border-off); }
  .nm { flex: 1; min-width: 0; font: 600 15.5px var(--f-display); color: var(--text); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .sz { font: 500 11.5px var(--f-value); color: var(--label); }
  .detail { flex: 1; min-width: 0; overflow-y: auto; padding: 22px 28px 28px; }
  .head { display: flex; align-items: center; gap: 14px; margin-bottom: 16px; }
  .head > div { flex: 1; min-width: 0; }
  .badge { width: 46px; height: 46px; display: grid; place-items: center; border-radius: 13px; border: 2px solid var(--c); color: var(--c); background: var(--bg); flex: none; }
  .menu { font: 600 12px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--c); }
  h2 { margin: 0; font: 700 30px/1.1 var(--f-display); }
  .danger {
    display: flex; align-items: center; gap: 7px; height: 36px; padding: 0 13px; border-radius: var(--r-sm);
    border: 1px solid var(--border-off); font: 600 15px var(--f-display); color: #ffb3b0;
  }
  .danger:hover { background: #2a1312; border-color: #6b2523; }
  .facts { display: flex; gap: 36px; margin: 0 0 8px; flex-wrap: wrap; }
  .facts dt { font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); }
  .facts dd { margin: 3px 0 0; font: 500 14.5px var(--f-value); }
  .where { margin: 0 0 18px; font-size: 13px; color: var(--label); }
  table { width: 100%; border-collapse: collapse; }
  th { text-align: left; font: 600 11.5px var(--f-display); letter-spacing: 0.08em; text-transform: uppercase; color: var(--label); padding: 0 12px 8px 0; border-bottom: 1px solid var(--line); }
  td { padding: 9px 12px 9px 0; border-bottom: 1px solid var(--line); vertical-align: top; font-size: 14px; }
  .pn { font: 600 15px var(--f-display); white-space: nowrap; }
  .tag { margin-left: 8px; padding: 1px 6px; border-radius: 4px; border: 1px solid var(--border-off); font: 600 10.5px var(--f-display); letter-spacing: 0.05em; text-transform: uppercase; color: var(--label); }
  .mono { font: 500 13px var(--f-value); white-space: nowrap; }
  .dim { color: var(--label); }
  .opts { display: flex; flex-wrap: wrap; gap: 4px; }
  .opts span { padding: 2px 7px; border-radius: 4px; background: var(--surface); border: 1px solid var(--line); font: 500 12.5px var(--f-display); }

  .veil { position: fixed; inset: 0; z-index: 60; background: rgba(5, 5, 4, 0.62); }
  .sheet {
    position: fixed; z-index: 61; left: 50%; top: 45%; transform: translate(-50%, -50%);
    width: min(620px, calc(100vw - 48px)); max-height: calc(100vh - 48px); overflow-y: auto;
    padding: 20px 22px 18px; border-radius: 14px; background: var(--raised); border: 1px solid var(--border-off);
    box-shadow: var(--float-shadow);
  }
  .sheet:focus { outline: none; }
  .sheet h3 { margin: 0 0 12px; font: 700 22px var(--f-display); }
  .sheet ul { list-style: none; margin: 0 0 12px; padding: 0; }
  .sheet li { display: flex; align-items: center; gap: 12px; padding: 9px 0; border-top: 1px solid var(--line); }
  .sheet li:last-child { border-bottom: 1px solid var(--line); }
  .ic { width: 34px; height: 34px; display: grid; place-items: center; border-radius: 9px; border: 2px solid var(--c); color: var(--c); flex: none; }
  .txt { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .txt b { font: 600 16px var(--f-display); }
  .txt small { font-size: 12.5px; color: var(--label); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .bad .txt small { color: #ffb3b0; white-space: normal; }
  .state { font: 600 11.5px var(--f-display); letter-spacing: 0.05em; text-transform: uppercase; color: var(--ok); white-space: nowrap; }
  .state.replace { color: #f6c15a; }
  .hint { margin: 0 0 14px; font-size: 13px; color: var(--label); line-height: 1.5; }
  .sheet footer { display: flex; justify-content: flex-end; gap: 8px; }
  .sheet footer button:not(.primary) { height: 36px; padding: 0 14px; border-radius: var(--r-sm); border: 1px solid var(--border-off); font: 600 15px var(--f-display); }
  .sheet footer button:not(.primary):hover { background: var(--track); }
</style>
