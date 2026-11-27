<script lang="ts">
  import Icon from './Icon.svelte';
  import { BROWSABLE, menuInfo, offered } from '../lib/menus';
  import { studio } from '../lib/store.svelte';
  import { t } from '../lib/i18n.svelte';
  import type { CatalogItem } from '../lib/types';

  let query = $state('');
  const menu = $derived(menuInfo(studio.browseMenu));
  const target = $derived(studio.selectedBlock);
  const special = (m: number) => m === 5 || m === 6 || m === 11;
  // FX Loop / Send / Return run on waveod's code in place of the original's (WVFX_REPLACE), but they are the
  // original's own entries: no "waveod" tag
  const replaced = (m: number) => m === 12 || m === 13 || m === 14;
  const items = (m: number) => (studio.catalogs[m] ?? []).filter((i) => offered(m, i));

  /** the menu's items by type, or the search's matches by menu */
  const groups = $derived.by(() => {
    const q = query.trim().toLowerCase();
    const out: { title: string; menu: number; items: CatalogItem[] }[] = [];
    if (q) {
      for (const m of BROWSABLE) {
        const found = items(m.menu).filter((i) => i.name.toLowerCase().includes(q));
        if (found.length) out.push({ title: m.name, menu: m.menu, items: found });
      }
      return out;
    }
    for (const item of items(studio.browseMenu)) {
      const title = item.type && item.type !== '-' && item.type !== '/' ? item.type : menu.name;
      let g = out.find((x) => x.title === title);
      if (!g) out.push((g = { title, menu: studio.browseMenu, items: [] }));
      g.items.push(item);
    }
    return out;
  });

  const hint = $derived(
    target ? t('browser.hint_replace', { name: target.name }) :
    studio.selection?.kind === 'cell' ? t('browser.hint_cell') :
    t('browser.hint'));

  function drag(e: DragEvent, m: number, item: CatalogItem) {
    e.dataTransfer?.setData('application/x-waveod-model', `${m}:${item.index}`);
    if (e.dataTransfer) e.dataTransfer.effectAllowed = 'copy';
  }
</script>

<aside class="browser">
  <nav class="rail" aria-label={t('browser.categories')}>
    {#each BROWSABLE as m (m.menu)}
      <button class:active={studio.browseMenu === m.menu && !query} style:--c={m.color} title={m.name}
              aria-label={m.name} aria-current={studio.browseMenu === m.menu}
              onclick={() => { studio.browseMenu = m.menu; query = ''; }}>
        <Icon name={m.icon} size={22} stroke={1.9} />
      </button>
    {/each}
  </nav>

  <div class="list">
    <label class="search">
      <Icon name="search" size={16} stroke={2} />
      <input type="search" placeholder={t('browser.search')} bind:value={query} spellcheck="false" />
    </label>
    <header style:--c={menu.color}>
      {#if query}
        <h2>{t('browser.results')}</h2>
      {:else}
        <h2>{menu.name}</h2>
        <span class="count">{items(studio.browseMenu).length}</span>
      {/if}
    </header>
    <p class="hint">{hint}</p>

    <div class="scroll">
      {#each groups as g (g.title + g.menu)}
        <section>
          <h3>{g.title}</h3>
          <ul>
            {#each g.items as item (item.index)}
              {@const here = target?.menu === g.menu && target?.id === item.id}
              <li>
                <button class="item" class:here style:--c={menuInfo(g.menu).color} draggable="true"
                        ondragstart={(e) => drag(e, g.menu, item)}
                        onclick={() => studio.choose(g.menu, item.index)}>
                  <span class="name">{item.name}</span>
                  {#if !special(g.menu) && !replaced(g.menu) && (item.kind === 'builtin' || item.kind === 'module')}
                    <span class="tag">waveod</span>
                  {/if}
                </button>
              </li>
            {/each}
          </ul>
        </section>
      {:else}
        <p class="empty">{query ? t('browser.none') : t('browser.reading')}</p>
      {/each}
    </div>
  </div>
</aside>

<style>
  .browser {
    display: flex;
    width: 312px;
    flex: none;
    border-right: 1px solid var(--line);
    background: var(--surface);
    min-height: 0;
  }
  .rail {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 10px 6px;
    border-right: 1px solid var(--line);
    overflow-y: auto;
  }
  .rail button {
    width: 44px;
    height: 44px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: var(--r-md);
    color: var(--off);
    transition: color 0.15s, background 0.15s;
  }
  .rail button:hover { color: var(--c); background: var(--raised); }
  .rail button.active { color: var(--c); background: var(--bg); box-shadow: inset 0 0 0 1px var(--border-off); }

  .list { flex: 1; min-width: 0; display: flex; flex-direction: column; }
  .search {
    display: flex;
    align-items: center;
    gap: 8px;
    margin: 12px 12px 6px;
    padding: 0 10px;
    height: 34px;
    border-radius: var(--r-sm);
    background: var(--bg);
    border: 1px solid var(--border-off);
    color: var(--label);
  }
  .search:focus-within { border-color: var(--dim); color: var(--text); }
  .search input {
    flex: 1;
    min-width: 0;
    background: none;
    border: 0;
    outline: none;
    font: 400 14px var(--f-body);
  }
  .search input::placeholder { color: var(--label); }
  header {
    display: flex;
    align-items: baseline;
    gap: 8px;
    padding: 10px 14px 0;
  }
  h2 {
    margin: 0;
    font: 700 22px/1.1 var(--f-display);
    color: var(--c, var(--text));
  }
  .count { font: 500 12px var(--f-value); color: var(--label); }
  .hint { margin: 4px 14px 8px; font-size: 12.5px; color: var(--label); line-height: 1.35; }
  .scroll { flex: 1; overflow-y: auto; padding: 0 8px 16px; }
  section + section { margin-top: 6px; }
  h3 {
    position: sticky;
    top: 0;
    z-index: 1;
    margin: 0;
    padding: 8px 6px 4px;
    background: var(--surface);
    font: 600 12px/1 var(--f-display);
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--label);
  }
  ul { list-style: none; margin: 0; padding: 0; }
  .item {
    display: flex;
    align-items: center;
    gap: 8px;
    width: 100%;
    padding: 7px 8px;
    border-radius: var(--r-sm);
    text-align: left;
    font: 500 16px/1.2 var(--f-display);
    color: var(--text);
    cursor: grab;
  }
  .item:hover { background: var(--raised); }
  .item.here { background: var(--bg); color: var(--c); }
  .name { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .tag {
    flex: none;
    font: 600 10.5px/1 var(--f-display);
    letter-spacing: 0.06em;
    padding: 3px 5px;
    border-radius: 4px;
    color: var(--accent);
    border: 1px solid #4d3814;
  }
  .empty { margin: 12px 8px; color: var(--label); font-size: 13px; }
</style>
