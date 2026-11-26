<script lang="ts">
  import { ICONS, type IconName } from '../lib/menus';

  let { name, size = 20, stroke = 2, color = 'currentColor' }: {
    name: IconName; size?: number; stroke?: number; color?: string;
  } = $props();
</script>

<svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke={color} stroke-width={stroke}
     stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
  <path d={ICONS[name]} />
</svg>
