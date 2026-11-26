// The pedal's 16 effect menus as its screens show them: name, tag, colours (src/ui/theme.c) and line icon
// (tools/mkicons.py: the design's SVG paths, 24 x 24, round caps).

export const ICONS = {
  DYN: 'M12 3v6M9 6l3 3 3-3M12 21v-6M9 18l3-3 3 3M4 12h16',
  WAH: 'M5 20l3-15h8l3 15zM9.5 10h5',
  DRV: 'M3 12h2l3-6h3v12h3l3-6h4',
  AMP: 'M3 6h18v12H3zM3 10h18M7 14v.5M11 14v.5M15 14v.5',
  CAB: 'M4 4h16v16H4zM8 12a4 4 0 1 0 8 0a4 4 0 1 0-8 0',
  EQ: 'M6 4v16M12 4v16M18 4v16M4 9h4M10 15h4M16 7h4',
  FLT: 'M3 8h7c3 0 4 2 5 5s2 5 6 5',
  MOD: 'M2 12c2-5 4-5 6 0s4 5 6 0 4-5 6 0',
  DLY: 'M3 12a4 4 0 1 0 8 0a4 4 0 1 0-8 0M14 9a3 3 0 0 1 0 6M18 10a2 2 0 0 1 0 4',
  RVB: 'M4 20A16 16 0 0 1 20 4M4 20a10 10 0 0 1 10-10M4 20a4 4 0 0 1 4-4',
  VOL: 'M4 9h4l5-4v14l-5-4H4zM17 9c1.5 1.5 1.5 4.5 0 6',
  SR: 'M4 8h14l-3-3M20 16H6l3 3',
  // the pedal draws FX Loop, Send and Return with SR; side by side in the browser they need a glyph each
  SND: 'M3 12h14M13 8l4 4-4 4M21 6v12',
  RTN: 'M21 12H7M11 8l-4 4 4 4M3 6v12',
  PLUS: 'M12 7v10M7 12h10',
  power: 'M12 4v8M7.5 7.5a6 6 0 1 0 9 0',
  trash: 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v6M14 11v6',
  prev: 'M15 5l-7 7 7 7',
  next: 'M9 5l7 7-7 7',
  down: 'M6 9l6 6 6-6',
  search: 'M10.5 4a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13M15.5 15.5L20 20',
  save: 'M5 4h11l3 3v13H5zM8 4v5h7V4M8 20v-6h8v6',
  close: 'M6 6l12 12M18 6L6 18',
  scene: 'M12 4l9 5-9 5-9-5zM3 14.5l9 5 9-5',
  grid: 'M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z',
  presets: 'M12 4l9 5-9 5-9-5zM3 14.5l9 5 9-5',
  chip: 'M7 7h10v10H7zM9 3v4M15 3v4M9 17v4M15 17v4M3 9h4M3 15h4M17 9h4M17 15h4',
  nam: 'M3 12h3l2-6 3 12 3-9 2 3h5',
  drum: 'M4 9c0-2 3.6-3.5 8-3.5s8 1.5 8 3.5-3.6 3.5-8 3.5S4 11 4 9zM4 9v6c0 2 3.6 3.5 8 3.5s8-1.5 8-3.5V9',
  pedal: 'M6 3h12v18H6zM9 16a3 3 0 1 0 6 0a3 3 0 1 0-6 0M9 7h6',
  play: 'M8 5.5v13l10.5-6.5z',
  stop: 'M7 7h10v10H7z',
  file: 'M6 3h8l4 4v14H6zM14 3v4h4M9 13h6M9 17h4',
  pencil: 'M4 20l1-4L16 5l3 3L8 19zM14 7l3 3',
  loop: 'M4 12a6 6 0 0 1 6-6h8l-3-3M20 12a6 6 0 0 1-6 6H6l3 3',
  once: 'M4 12h13M13 8l4 4-4 4M20 6v12',
  link: 'M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1',
  plus: 'M12 5v14M5 12h14',
  globe: 'M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M3 12h18M12 3c2.5 2.5 3.5 5.5 3.5 9s-1 6.5-3.5 9c-2.5-2.5-3.5-5.5-3.5-9s1-6.5 3.5-9',
  io: 'M3 12h5M16 12h5M18.5 9.5L21 12l-2.5 2.5M8 7h8v10H8z',
  tuner: 'M4 17a8 8 0 0 1 16 0M12 17l4-6M12 6v1.5M6.3 9.3l1 1M17.7 9.3l-1 1',
  usb: 'M12 3v14M9.5 5.5L12 3l2.5 2.5M8 9v2.5l4 3M16 8v3.5l-4 3M10 17h4v4h-4z',
  fs: 'M4 19h16M6 19v-3h12v3M12 16v-5M9 7.5a3 3 0 1 0 6 0a3 3 0 1 0-6 0',
  midi: 'M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M7.5 12.5v.01M16.5 12.5v.01M9 8.5v.01M15 8.5v.01M12 7v.01M10 17h4',
  screen: 'M4 5h16v11H4zM9 20h6M12 16v4',
  sliders: 'M4 7h9M17 7h3M4 17h3M11 17h9M13 4v6M7 14v6',
  mic: 'M9 3h6v9a3 3 0 0 1-6 0zM5 11a7 7 0 0 0 14 0M12 18v3M9 21h6',
} as const;

export type IconName = keyof typeof ICONS;

export interface MenuInfo {
  menu: number;
  tag: string;
  name: string;
  color: string;      // bright: borders, icons, arcs
  dark: string;       // the block's fill when it is selected
  icon: IconName;
}

const RAW: [string, string, string, string, IconName][] = [
  ['DYN', 'Dinâmica', '#17D0D8', '#002528', 'DYN'],
  ['FX', 'Efeitos', '#E698DF', '#2B1729', 'FLT'],
  ['WAH', 'Wah', '#FE93A4', '#31151A', 'WAH'],
  ['DRV', 'Drive', '#FF958D', '#321614', 'DRV'],
  ['AMP', 'Amp', '#F4A25C', '#2F1A07', 'AMP'],
  ['CAB', 'Gabinete', '#D1B64A', '#261F03', 'CAB'],
  ['IR', 'IR', '#D1B64A', '#261F03', 'CAB'],
  ['EQ', 'EQ', '#80CD82', '#102511', 'EQ'],
  ['MOD', 'Modulação', '#C4A4FE', '#231A31', 'MOD'],
  ['DLY', 'Delay', '#73BDFF', '#0D2034', 'DLY'],
  ['RVB', 'Reverb', '#36D2BA', '#002620', 'RVB'],
  ['CLN', 'Clone', '#FD9976', '#31170E', 'AMP'],
  ['LOOP', 'FX Loop', '#9FB0FF', '#191D34', 'SR'],
  ['SND', 'Send', '#9FB0FF', '#191D34', 'SND'],
  ['RTN', 'Return', '#9FB0FF', '#191D34', 'RTN'],
  ['VOL', 'Volume', '#B5C159', '#1F2206', 'VOL'],
];

export const MENUS: MenuInfo[] = RAW.map(([tag, name, color, dark, icon], menu) => ({ menu, tag, name, color, dark, icon }));

/** the menus the browser offers: all of them, FX Loop, Send and Return included (ui_menus, since waveod runs them) */
export const BROWSABLE = MENUS;

/** the original's Split, Path B and Merge (EQ entries, src/fw/library.h ID_SPLIT..ID_MERGE): its way of making
 *  a dual chain, which the grid's routing does now. Presets that have them still show them; the browser does not
 *  offer them. */
const ROUTING_IDS = new Set([0x01000070, 0x01000071, 0x01000072]);
export const offered = (menu: number, item: { id: number }) => !(menu === 7 && ROUTING_IDS.has(item.id));

export function menuInfo(menu: number): MenuInfo {
  return MENUS[menu] ?? { menu, tag: '?', name: '?', color: '#8A8780', dark: '#1F1E1C', icon: 'PLUS' };
}
