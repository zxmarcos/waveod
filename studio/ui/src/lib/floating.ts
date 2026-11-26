// A pop-up list for a control that lives inside a clipped box (the knob row scrolls sideways, so anything
// absolutely placed inside it is cut off): the element moves to <body> and is placed next to its anchor, on the
// side with more room, inside the window. It closes on a click elsewhere, a scroll outside it or a resize.
//
//   <ul use:floating={{ anchor: button, onclose: () => (open = false) }}>

export interface FloatingOptions {
  anchor: HTMLElement;
  onclose: () => void;
  maxHeight?: number;
}

const MARGIN = 8;
const GAP = 6;

export function floating(node: HTMLElement, opts: FloatingOptions) {
  document.body.appendChild(node);
  node.style.position = 'fixed';

  function place() {
    const a = opts.anchor.getBoundingClientRect();
    const above = a.top - MARGIN - GAP;
    const below = window.innerHeight - a.bottom - MARGIN - GAP;
    const up = above > below;
    node.style.maxHeight = `${Math.min(opts.maxHeight ?? 360, Math.max(96, up ? above : below))}px`;
    const w = node.offsetWidth, h = node.offsetHeight;
    const left = Math.min(Math.max(MARGIN, a.left + a.width / 2 - w / 2), window.innerWidth - w - MARGIN);
    node.style.left = `${left}px`;
    node.style.top = `${up ? a.top - GAP - h : a.bottom + GAP}px`;
    node.dataset.side = up ? 'up' : 'down';
  }

  const outside = (t: EventTarget | null) =>
    !(t instanceof Node) || (!node.contains(t) && !opts.anchor.contains(t));
  const onDown = (e: PointerEvent) => { if (outside(e.target)) opts.onclose(); };
  const onScroll = (e: Event) => { if (!(e.target instanceof Node && node.contains(e.target))) opts.onclose(); };
  const onResize = () => opts.onclose();

  place();
  window.addEventListener('pointerdown', onDown, true);
  window.addEventListener('scroll', onScroll, true);
  window.addEventListener('resize', onResize);

  return {
    update(next: FloatingOptions) {
      opts = next;
      place();
    },
    destroy() {
      window.removeEventListener('pointerdown', onDown, true);
      window.removeEventListener('scroll', onScroll, true);
      window.removeEventListener('resize', onResize);
      node.remove();
    },
  };
}

/** arrow keys, Home and End move the focus among the buttons inside `list`; true when the key was used */
export function moveFocus(list: HTMLElement, e: KeyboardEvent): boolean {
  const items = [...list.querySelectorAll<HTMLElement>('button:not(:disabled)')];
  if (!items.length) return false;
  const at = items.indexOf(document.activeElement as HTMLElement);
  let to = -1;
  if (e.key === 'ArrowDown') to = at < 0 ? 0 : Math.min(items.length - 1, at + 1);
  else if (e.key === 'ArrowUp') to = at < 0 ? items.length - 1 : Math.max(0, at - 1);
  else if (e.key === 'Home') to = 0;
  else if (e.key === 'End') to = items.length - 1;
  if (to < 0) return false;
  e.preventDefault();
  items[to].focus();
  return true;
}
