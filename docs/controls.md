# Controls

The pedal has a big MENU encoder (with push), three endless knobs under the display, eight buttons (EXIT, EFX,
SAVE, DRUM, GLOBAL, CTRL, EXP, MENU push), four footswitches A-D and BANK up / down.

The navigation follows the "Fluxo de Uso" design (claude.ai/design, same project as the screens):

1. **Tap opens a block, tap steps on a footswitch.** A block on the home screen opens its edit screen; a card of the
   STOMP or CENAS tab is a footswitch: a tap does what stepping on it does (holding it opens its detail).
2. **Hold switches** on the home and edit screens. Holding a block grows a bar along its bottom; when it
   is full (0.5 s) the block switches on / off. Lifting the finger earlier cancels.
3. **On the chain, hold = drag.** On / off is the ON / OFF switch next to the name there.
4. **The knobs follow the screen.**
5. **‹ or EXIT go back one level.** Holding EXIT goes home from anywhere. Home is the tab of the footswitch mode
   (PRESET, STOMP or CENAS): going back or home never changes what the footswitches do.
6. **≡ and the mode on top.** ≡ opens MENU (CHAIN, TUNER, PRESETS, CONTROLES, LOOPER, DRUMS, GLOBAL EQ, GLOBAL);
   the mode button (its icon: PRESET house, STOMP, CENAS) opens a chooser of the three footswitch modes, and choosing
   one shows its tab. CHAIN and TUNER are main screens too (no way back); MENU is left when a place is chosen.
7. **Swipe the preset name** to change presets; **tap it** for the list of presets (PRESETS, below).

Main flow: PRESET → tap a block → EDIT → tap the model name → SELECIONAR EFEITO (tap a model: it is loaded and heard at
once) → ‹ → EDIT → ‹ → PRESET.

The top bar is 44 px: a row of buttons 36 px high (each taking taps over the bar's whole height, ~9 mm) and under
it a 4 px strip with the meters: IN on the left, OUT in the middle (-48..0 dBFS of the converters, 2 dB per segment,
green / amber / red, with a falling peak marker; the output meter is what leaves the pedal, after the preset level,
MASTER and the tuner's mute, at the DAC's full scale: a sample of 1.0 in the chain is -6.8 dBFS there, as in the
original's output loop) and the DSP load on the right (the slowest audio block against the 1.45 ms the block lasts;
amber from 75 %, red from 90 %). MENU shows the same meters with their names and the DSP load in %. On the main
screens the row has ≡, the footswitch mode (framed on its own tab), the preset (number, name; swipe it sideways =
next / previous preset, tap = the PRESETS list) with a BPM / VOL button inside it (tempo and volume on the chain
screen, lit amber while they are shown; tap again = back to the block), the scene (C1-C4, see Scenes: always on the
CHAIN tab, on the PRESET / STOMP tabs while the scenes are in play, not on CENAS) and SAVE, lit while the preset has
unsaved changes; on the other screens ‹ and the screen's name, and on EDIT and MIC the scene at the right end. The
mode chooser and the scene menu also take MENU turns / press; EXIT closes them, the footswitches keep working.

At power-up the waveOD logo animates for about 2.5 s while the sound already runs; any control or touch skips it.

## Everywhere

| Control | Action |
|---|---|
| Footswitches A-D, BANK -/+ | What the preset assigns (see "Footswitches" below); factory presets: PRESET tab (preset mode) A-D = preset A-D of the bank (the active one again = the preset CTRL), STOMP tab (stomp mode) A-D = CTRL 1-4; BANK -/+ = previous / next bank. CENAS tab (scene mode): A-D = scenes 1-4, BANK -/+ = previous / next preset. The other screens keep the mode of the last of these three tabs; a mode chosen there (pair, MIDI) keeps the screen and says so in a toast |
| Two footswitches together | Factory presets: A+B = tuner, C+D = looper, BANK- + BANK+ = the next mode (preset → stomp → scenes) |
| MASTER knob | Output volume (squared, like the stock firmware) |
| SAVE | Save screen (see below) |
| GLOBAL | Settings (see below) |
| EXIT | One level back; on CHAIN / TUNER: home (the footswitch mode's tab) |
| Hold EXIT | Home (the footswitch mode's tab) |
| EXP pedals | Their links in the preset (EXP 1A / 1B / 2, see "Pedals" below); pressing EXP 1 past its toe switch presses the toe key (factory: flips A / B) |
| EXP button | The links of EXP 1 (its current group); on the edit screen: what controls the parameter turned last (see "DE ONDE") |
| Hold EXIT at power-up | Safe mode: the power-up preset is not loaded (an empty chain named MODO SEGURO in its place) |

## Footswitches

Every preset has a table of links: for each footswitch mode (preset, stomp, scenes) and key (A, B, C, D, BANK-,
BANK+, the pairs A+B, B+C, C+D, BANK- + BANK+, and in preset mode the active preset's key again) what happens on a
press (run when the footswitch is released), on a long press, or while it is down (momentary). A key can have
several links; pairs run when released. Bank and preset steps repeat while held. A key the preset has no links on
(in that mode, for that kind of press) plays the global table instead (see "Global controls" below). The pedal
starts in the mode it was left in, however it was chosen (tab, EFX, the pair, MIDI; preset / stomp: the global
settings; scene mode: waveod's settings).

| Link | Action |
|---|---|
| Block | The block on while the key is on (or inverted: off while it is on) |
| Parameter | A parameter of a block at its ON value while the key is on, at its OFF value otherwise |
| Scene | Scene 1-4, the next or the previous one |
| Function | One of the functions below |

A key's block and parameter links share one state: a press flips it and they all follow (a drive on, the chorus off
and the delay's feedback up at once); a momentary link holds the ON side while the footswitch is down. The state
is read back from the first of them (the block's switch, or which value the parameter is nearer), so switching that
block elsewhere moves the key too. Removing a block removes its links; another effect in its place drops its
parameter links.

Presets saved before the table (and the factory presets) get one from what the stock firmware stored: CTRL 1-4
become block links of their members, the preset CTRL the active key's links; in scene mode A-D recall scenes 1-4,
BANK -/+ step to the previous / next preset and the pairs are preset mode's. What is the factory presets' (preset
A-D, banks, the pairs, scene mode, the toe switch) is left to the global table. Such a preset keeps the stock fields
until its table is changed; from then on the table is saved instead (the stock editor's footswitch and CTRL
pages no longer act on the pedal).

| Function | Action |
|---|---|
| Preset A-D | Load that preset of the bank; the active one again runs its "preset again" links |
| Preset - / + , Bank - / + | Previous / next preset or bank |
| Banks (BANCOS) | The PRESETS list, the original's bank menu: BANK -/+ move through the banks, A-D load and close it |
| Tap | Tap tempo (40..300 BPM): the global tempo when it is on, otherwise the preset's; ignored while the tempo comes from a MIDI clock (GLOBAL › MIDI: FONTE DO TEMPO) |
| Tuner | Tuner |
| EXP A/B | EXP 1 to the other group (its wah and volume blocks follow) |
| Mode | The next mode: preset → stomp → scenes → preset (the pedal starts in the mode it was left in) |
| Looper | The looper screen (below) |
| Drum machine | Start / stop; RITMO + / - the next / previous pattern |

### Pedals

The toe switch (BICO DO EXP 1) is a key of its own, the same in every mode: by default it flips EXP 1 between
groups A and B. EXP 1A (EXP 1 while in group A), EXP 1B and EXP 2 have links of their own, also the same in every
mode:

| Pedal link | Action |
|---|---|
| Parameter | From its heel value to its toe value along a curve: LINEAR, LENTA (fine near the heel) or RÁPIDA (fine near the toe); any number of them on one pedal |
| Block, on when it moves | On as soon as the pedal leaves the heel, off after 0.5 s back at the heel (a wah that engages itself) |
| Block with the group | EXP 1A / 1B: on while EXP 1 is in that group, off in the other |

As the stock firmware, the wah and volume blocks of EXP 1's parameter links follow the group too. Parameters with
20 steps or more glide to each new position over about 12 ms (no zipper noise); switches and short ranges change
at once. Pedal moves and auto switching are not edits: the preset does not become EDITADO and the scenes keep
their values. A preset from before the links gets its stock EXP targets (min at the heel, max at the toe).

### CONTROLES (CTRL on the PRESET, STOMP or CENAS tab)

The footswitches and the pedals drawn where they sit (A-D along the bottom, BANK − and BANK + above C and D, EXP 1
and EXP 2 over A and B), each card with its links in the mode shown, one line each (SEG = on the long press; on EXP
1's card A / B = the group, BICO = the toe switch) and how many there are, its border in the key's colour. MENU
turns pick a card, MENU press (touch: tap) opens its links; EFX shows the next mode; EXIT or CTRL go back. The
pairs and the active preset's key are on the links screen (TECLA). Below the cards, MIDI: the CCs that have links
(every mode), each with its first one; it opens the first CC's links (CC 1 when there is none: learn one on TECLA).
Right of it, KNOBS: what the preset's three quick knobs turn; it opens the quick knobs' assignments (below). MIDI and
KNOBS are the last stops of the page's MENU turns; on the global table there is no KNOBS.

The button at the top right (or CTRL held; a press of CTRL still goes back) switches to the list view, which the
pedal then keeps (waveod's settings): a row per key, grouped (PÉS A-D and BANK −/+; PARES with the active
preset's key in preset mode; PEDAIS: the toe switch, EXP 1A / 1B, EXP 2; MODULADORES; MIDI: the CCs that have
links; KNOBS), each with what it does in the mode shown, its first two links joined and +n for the others, grey
with a G tag when they are the global table's, SEM LIGAÇÕES when none, NADA (SEM AS GLOBAIS) when a NADA link keeps
the global ones out. MENU turns move the cursor over the rows, MENU press (or a tap) opens the key's links screen
(the pairs straight away, without TECLA), EFX shows the next mode, dragging scrolls. On the global table the same,
without the active preset's key, the modulators and KNOBS.

A second page, CONTROLES · MODULADORES (on a preset only): a card per modulator, MOD 1-4, with its type and rate,
a small drawing of it playing and its first link (+n for the others); a card opens the modulators screen on it
(Modulators, below). MENU turns go on from KNOBS to MOD 1-4 and the page follows; swipe sideways or tap a page's
segment (along the bottom) for the other page.

### A key's links

The screen shows the mode the pedal is in, or the one CONTROLES showed (EFX: the next mode; the title says which).

| Row | Knob 1 | MENU press |
|---|---|---|
| TECLA | key, then the toe switch, the pedals and MIDI CC 1..127 (the modulators: their own screen, below) | learn: the next footswitch pressed, pedal moved or MIDI CC received (EXIT stops) |
| NOME NO CARD | | keyboard (8 characters; empty = from the links) |
| COR | automatic (the first block's category, white for parameters) or a LED colour | |
| a link (APERTAR / SEGURAR / ENQUANTO PISA, PEDAL, MIDI) | | where it goes (PARA ONDE, below) |
| + NOVA LIGAÇÃO | | a new one, on PARA ONDE (dropped if left without a choice) |

MENU held (touch: hold the row) removes a link. A link shared by several modes (converted presets) becomes the
edited mode's own when it is opened. Touch: tap a row to select it, tap again = MENU press, drag it sideways =
knob 1.

### PARA ONDE (where a link goes)

Laid out like the models screen: the destinations (the chain's blocks with their category's icon, then Cenas,
Funções and MIDI; a pedal: blocks and MIDI), what of it, and when. Picking a choice sets the link at once.

| Column | Footswitch | Pedal (EXP 1A / 1B / 2) | MIDI CC |
|---|---|---|---|
| 1 | blocks, Cenas, Funções, MIDI | blocks, MIDI | blocks, Cenas, Funções, MIDI |
| 2 | a block: LIGA O BLOCO, DESLIGA O BLOCO, its parameters; the scenes 1-4, next, previous; NENHUMA (REMOVER), then the functions; ENVIAR CC, ENVIAR PC | a block: LIGA AO MOVER, COM O GRUPO (not EXP 2), its parameters; ENVIAR CC | as a footswitch |
| 3 | APERTAR, SEGURAR, PISANDO (while down; not the toe switch) | the curve of a parameter or CC: LINEAR, LENTA, RÁPIDA | as a pedal |

NENHUMA (REMOVER), the first of the functions, removes the link like MENU held: a key without links does nothing
(a pair still takes both footswitches, so neither runs alone).

Below, a parameter link's values: OFF / ON (a pedal: CALCANHAR / PONTA; a MIDI CC: EM 0 / EM 127) and when / the
curve. A MIDI out link has five: the CC or program number, the channel (1-16), when / the curve, and the CC's two
values (0 and 127 at first). Controls: MENU turns = the cursor through every choice, MENU press = take it, MENU held
= remove the link; knobs 1-2 = the values (MIDI out: number / channel; other links: destination / choice), knob 3 =
when / curve; EXIT = back. Touch: tap a destination, a choice or a when; drag a column to scroll; drag a value
sideways (a MIDI out CC's OFF / ON only this way).

### DE ONDE (hold a parameter on the edit screen, or EXP there)

The footswitches and the pedals drawn like CONTROLES; the cards that already move the parameter are lit with their
values (A / B / BICO on EXP 1's card; SEG = on the long press; PRESET / CENAS when not in stomp mode), the others
say how many links the key has already (JÁ FAZ n). Press a footswitch (held = a link on the long press), move a
pedal, or tap a card: it gets a link to the parameter and PARA ONDE opens on it for its values; tapping a lit card
opens its link. A footswitch gets it in stomp mode with OFF = the value now and ON = the other end of the range,
joining the key's other links (one press switches them together); a pedal sweeps the whole range, heel to toe, in
every mode. Pressing the toe switch learns the toe key; a pedal counts once it has moved a quarter of its range and
rested for 0.3 s, so a stomp on the toe switch is taken as the toe switch.

A MIDI CC received while the screen is shown links the parameter to that CC (every mode, the whole range from 0 to
127); the MIDI box below lights with it (CC n and its values).

Below: UM VALOR POR CENA (tap or EFX: a scene parameter or the preset's, see Scenes), MIDI and the parameter (knob 1:
another of the block). MENU turns pick a card (MIDI last), MENU press = tap, MENU held (touch: hold) removes the
card's links to the parameter; EXIT or EXP = back.

### Modulators (CONTROLES › MODULADORES)

The preset's four modulators (an LFO, an envelope of the guitar, a step sequencer; docs/modulators.md) are keys
of the control table after the pedals, edited on their own screen, MODULADORES: a card of CONTROLES' second page, or any
modulator link opened elsewhere. Tabs MOD 1-4 (knob 2 / EFX / tap), a live drawing (the LFO's shape with the point
running along it, the sequence's steps, or the last seconds of the envelope and the random shapes), then TIPO, the
type's settings (FORMA, SINCRONIA, VELOCIDADE, FASE INICIAL / SENSIBILIDADE, ATAQUE, RETORNO / SINCRONIA,
VELOCIDADE, PASSOS, DESLIZE and a row per step; knob 1 turns a value, MENU press steps it) and the links. Its
links are parameters of the chain's blocks only (PARA ONDE offers nothing else), from MOD 0% to MOD 100% along a
curve, moved in the audio interrupt every block. Not on the global table.

### MIDI in the links

A MIDI control change is a key like the pedals, in every mode (not CC 0, bank select, and not 68 / 69, the tempo).
Its value moves its parameter links and MIDI out CCs (0..127 as heel..toe, along the curve); its blocks are on from
64 up (DESLIGA O BLOCO: off); its functions, scenes and program changes run from 64 up. A CC with links in the
preset does that instead of its fixed meaning (the table in the original's MIDI chart, CC 30 for the scenes...),
for that preset only.

A MIDI out link sends on both MIDI outputs (TRS and USB), on its own channel:
- from a footswitch, a CC is a state like a block: a press flips it (ON value, then OFF), PISANDO holds it at ON
  while down, and with the key's blocks or parameters it follows them; the LED shows it. A program change is sent
  on every press;
- from a pedal or a MIDI CC, a CC sweeps from its CALCANHAR / EM 0 value to its PONTA / EM 127 value, sent when
  the value changes.
The state is what was sent last, forgotten when a preset loads (OFF until sent).

The edit screen names in each parameter's corner what moves it: CENA, the first key or pedal ("+" when more),
or both.

## Global controls (GLOBAL › CONTROLES)

A second table of links, the pedal's own, kept apart from the presets (waveod's store). It holds what has nothing
to do with a preset's blocks: functions, scenes and MIDI out (CC / PC), for the same keys (footswitches, pairs, the
toe switch, the pedals, MIDI CCs) and modes. A key's group in a mode (its press and while-down links, or its long
press links) runs from the preset when the preset has a link there, otherwise from the global table. So a preset
that does nothing special with BANK + plays the global BANK +, and one with its own BANK + link plays only that.

At first the global table holds what the factory presets' footswitch functions do (preset mode A-D = preset A-D,
press and hold; BANK -/+ = bank; A + B tuner, C + D looper, BANK - + BANK + the next mode, in every mode; scene
mode A-D = scenes 1-4, BANK -/+ = previous / next preset; the toe switch = EXP A/B). A preset converted from the
stock fields leaves those groups to it when they are the factory ones (in the 240 factory presets everything but
stomp mode's CTRL 1-4 or TAP, the preset CTRL and the EXP targets), and gets a NADA link where its field is empty
but the global table has links. With the global table untouched every preset plays as before; changing it changes
every preset that has no link of its own on that key. Presets whose table was stored by waveod before the global
table keep all their links (their own, so the global table does not reach those keys).

CONTROLES GLOBAIS is the CONTROLES screen on the global table (title CONTROLES GLOBAIS): the same cards, links
screen and PARA ONDE, without blocks (Cenas, Funções and MIDI only) and without the active preset's key. The table
is written to the flash when those screens are left. Removing a link there (MENU held, or NENHUMA (REMOVER))
takes it out of every mode it is in, toast REMOVIDA EM TODOS OS MODOS: C + D's looper goes from the pair in preset,
stomp and scene mode at once (on a preset a removal stays the edited mode's). There is no NADA link in the global
table: a key without links already does nothing; NADA belongs to a preset, where it keeps the global links out.
waveod Studio's controls editor does the same (Nenhuma (remover a ligação) first among the functions; the remove
button of a global link: from every mode).

On a preset, CONTROLES shows the global links that play there in grey ("(G)" on the MIDI bar); the links screen
lists them as GLOBAL · APERTAR / SEGURAR rows: MENU press on one copies its group into the preset (to edit it
there), MENU held gives the group a NADA link instead (NADA (SEM AS GLOBAIS): the key does nothing there in this
preset; removing that link brings the global ones back). + NOVA LIGAÇÃO on a key whose press group is the global
one copies the group first, so the new link joins it. A NADA link on a MIDI CC does not take the CC's fixed meaning
away.

## Footswitch LEDs

Each key's LED shows its press links in the active mode. A key with block or parameter links: lit while its state is
on, in the key's colour, else the first block's category colour (DYN / AMP / RVB cyan, FX / EQ green, WAH / VOL
magenta, DRV red, CAB / IR / Clone yellow, MOD / DLY blue), white for parameters only; off otherwise. TAP blinks yellow on the beat, preset A-D white on the active preset, BANK / preset steps white /
yellow, tuner and looper green, drums red, EXP A/B green on A and red on B, mode blue; in scene mode the active scene's key
cyan, the others off. The EXP LED shows the EXP 1
group (green A, red B) everywhere. The tuner and any warning switch the key LEDs off; the looper screen shows the
looper on A and B (red recording, green playing, blue overdubbing, green blinking stopped, red blinking armed, off
for 250 ms when the loop restarts) and AUTO REC on D (cyan).

## After a crash

A fault or a stopped program restarts the pedal within about 1.5 s (3 s with interrupts off), with the outputs
silent meanwhile. After the restart a warning tells the preset, the block and the cause. When a block of the
power-up preset failed soon after the start-up, or the pedal failed twice in a row, that preset is not loaded
(MODO SEGURO): pick another preset, or fix it with the editor.

## Quick knobs and EXP 1 (KNOBS on CONTROLES; hold a quick knob on the PRESET tab)

What the quick knobs turn and where EXP 1 starts, as the stock editor's preset settings. Holding a quick knob on the
PRESET tab opens this on its row. Rows and the knobs:

| Row | Knob 1 | Knob 2 | Knob 3 | MENU press |
|---|---|---|---|---|
| KNOB 1-3 | target: a block, BPM, volume | parameter | | next target |
| EXP 1 COMEÇA EM | A / B | | | flip |

Changes make the preset EDITADO and are saved with it.

## Looper

Opened by the footswitch function LOOPER (factory presets: C+D). The screen: the state (red while recording or
overdubbing, green while playing) and the time, the loop's waveform (peaks on a -42..0 dB scale, brighter up to the
playback position, a white line there; while recording it spans 8 s and grows ahead of the recording; the last
overdub shows until it is undone), the levels and a card per footswitch. The footswitches belong to the looper there:

| Control | Action |
|---|---|
| A (press) | Record, then play, overdub, play...; with AUTO REC the recording (or overdub) waits for the guitar |
| B (press) / held | Stop / erase the loop |
| C (press) | Undo / redo the last overdub |
| D (release) | AUTO REC on / off |
| C + D, EXIT | Leave (the loop keeps playing) |
| Knob 1 / 3 | Record / playback level (100 / 100 = unity) |
| Knob 2 | PRÉ (mono, before the effects: the loop goes through them) / PÓS (stereo, after them); only while empty |
| MENU press | Erase the loop (stopped) |

Up to 90 s (PRÉ) or 45 s (PÓS) with undo, which the stock firmware keeps on; the levels and the place are the
stock firmware's global settings.

## Drum machine (DRUM)

The stock firmware's 100 patterns in 8 genres (Rock, Blues, Funk, Jazz, Pop, Electronic, World, Metronome) and
its sounds, taken from the base firmware when the update file is made (`tools/mkfw.py`). Like the original's screen:
the genres (ESTILO) and the genre's patterns (RITMO) in two lists, the chosen one marked (green while playing), a
play / stop button with the bar's four beats (the first green), and the tempo / volume below.

| Control | Action |
|---|---|
| DRUM, MENU press, EFX | Start / stop |
| MENU turn | Pattern |
| Knob 1 | Genre |
| Knob 2 | Tempo; when it follows the effects' tempo, that tempo (the preset's, or the global one) |
| Knob 3 | Volume |
| CTRL | Follow the effects' tempo on / off (tap tempo then sets both) |
| EXIT | Back (the drums keep playing) |

Touch: tap a genre or a pattern to choose it, drag a list up / down to scroll it, the button = start / stop, tap
TEMPO = follow on / off, drag a value sideways. Pattern,
tempo, volume and following are the stock firmware's global settings.

**MEUS RITMOS** (waveod's, the last style): up to 20 patterns imported from MIDI files with waveod Studio (tab
Bateria). The list shows the 20 places ("vazio" for an empty one); MENU turns, RITMO + / − and a drag skip the empty
ones. Choosing one plays it instead of the bank's pattern (kept as a waveod setting, so the stock firmware keeps its
own choice) and sets the drums' tempo to the MIDI file's unless they follow the effects' tempo. A pattern made to
play once (a whole song) stops at its end: the button goes back to play, and play starts it from the beginning
again. The RITMO cell shows M1-M20 (UMA VEZ for those). Choosing a pattern of the bank, from here, the footswitches,
MIDI or the stock editor, goes back to the bank. MIDI CC 94 values 100-119 choose M1-M20.

The 13 sounds of the bank: electronic kick, kick, side stick / click, electronic snare, clap, snare, closed hi-hat,
open hi-hat, crash, ride, splash, cowbell, tambourine (named from their spectra and from where the factory
patterns use them; 7, 10, 11 and 12 are in no factory pattern). There are no toms: the Studio leaves the toms of a
MIDI file out and says so.

## PRESETS (the list of presets)

The 240 presets as 60 banks of four, a row per bank with A-D like the footswitches: the active preset amber (with
* when edited), the cursor framed, a mark on the right for where the five rows shown are. It takes the place of the
original's bank screen.

Opened by a tap on the preset in the top bar or on the name on the PRESET tab, by the footswitch function BANCOS and
by MIDI CC 26-28 (from 64; below: back to the main screen).

| Control | Action |
|---|---|
| MENU turn | Cursor |
| Knob 1 | Cursor a bank down / up |
| MENU press | Load the cursor's preset (the list stays) |
| BANK - / + | Cursor a bank down / up; nothing loads |
| A-D | Load that preset of the cursor's bank and close the list |
| EXIT | Back |

The footswitches belong to the list while it is shown (in every footswitch mode), like the original's bank menu:
BANK -/+ blink cyan, A-D are cyan with the active preset's key white. Touch: tap a preset = load it (the list
stays), drag the list up / down = scroll it. Loading another preset drops unsaved changes, as everywhere else.

## 01 Preset (tab PRESET, home)

The chain as blocks: in series one row; with parallel lanes (chain tab) the PRE blocks, lanes A and B stacked
between two dividers (split and merge; dual L/R has no merge), and the POST blocks. Below: number, name, amp and cab,
the bank's four presets as dots ("EDITADO" when the preset has unsaved changes), and the three quick knobs.

| Control | Action |
|---|---|
| MENU turn | Previous / next preset |
| MENU press | CHAIN tab |
| EFX | The next footswitch mode: PRESET → STOMP → CENAS → PRESET (its tab) |
| Knobs 1-3 | Quick knobs: what the preset assigns (a block's parameter, the preset BPM or volume) |
| CTRL | CONTROLES: footswitches, pedals, MIDI and quick knobs (as on the STOMP and CENAS tabs) |

## 02 Stomp (tab STOMP)

One card per footswitch with its stomp mode links, placed like the footswitches on the pedal: A-D along the
bottom, BANK − and BANK + above C and D, the preset's number and name in the corner over A and B. The card shows
the key's name, or its first block ("+N" when it has more links), or the parameter it moves, or its scene or
function; lit (in the key's colour) while its state is on. Factory presets keep BANK -/+ as previous / next bank.
Tap a card = what the footswitch does (as stepping on it), hold it = its block's edit screen (no block: the key's links,
CONTROLES). EFX goes to the next mode (CENAS), MENU press
opens the CHAIN tab, CTRL the footswitches and pedals (CONTROLES); the knobs work as on the home screen.

## Scenes (tab CENAS, waveod's)

A scene is a state of the same preset: which blocks are on and the values of its scene parameters. Recalling one
switches blocks and sets values without reloading the chain (no gap in the sound; delay and reverb tails behave as
when a block is switched by hand) and does not count as an edit. Each preset has four; one without scenes has the
four equal to its blocks.

- **Editing is on the chain:** like snapshots on other pedals, a scene is edited where the sound is: the CHAIN tab
  and the block screens (EDIT, MIC). The scene button in the top bar (C1-C4) shows the active scene, amber while the
  scenes are in play (scene mode, a preset whose scenes differ, or a scene chosen in the scene menu since the preset
  was loaded): then the edits go to that scene. The block bar's ON / OFF shows it too (ON C2).
- **Scene menu (tap the scene button):** the four scenes with their names and blocks; tap one = it plays and edits
  go to it. COPIAR PARA... then a scene = the active scene's blocks and scene values over it (CANCELAR, EXIT or the
  source again = no copy); RENOMEAR = the active scene's name (up to 10 characters; "CENA n" when none); DESFAZER =
  the active scene as the stored preset has it (blocks and scene parameters added since keep theirs). Swipe the
  scene button sideways = previous / next scene. MENU turns / press work in the menu.
- **What a scene keeps:** while the scenes are in play, switching a block (touch, EFX, footswitch, CTRL, MIDI)
  changes the active scene only, and (KNOBS NA CENA: AUTO, GLOBAL › PRESETS, the default) a parameter turned there
  becomes a scene parameter: the other scenes keep the value it had, the active one gets the new one (marked CENA on
  the chain and edit screens). Pedals, footswitch parameter links, MIDI links and a pasted block never make one.
  To make a parameter the same in every scene again: hold it on the edit screen, UM VALOR POR CENA (at the value it
  has now). With KNOBS NA CENA: MANUAL, only the parameters marked there have values per scene. Up to 32 per preset
  (a notice when full: further parameters stay the preset's).
- **Saved with the preset:** SAVE stores the scenes and which one is active; loading the preset plays that one.
  The expression pedals move parameters without changing the scenes.
- **Tab CENAS (for playing):** the four scenes on A-D like the stomp tab's cards, with a mark per block of the chain
  (its colour when on in that scene); the active one lit. BANK − / + above C and D show the previous / next preset.
  The corner shows the preset and the active scene.
- **Touch:** tap a scene = recall it, hold it = edit it (the CHAIN tab in that scene); tap BANK − / + = previous /
  next preset. The knobs, MENU and CTRL work as on the PRESET tab; EFX goes to the next mode (PRESET).

MIDI CC 30 recalls a scene (values 0-3; unless the preset links CC 30, see MIDI in the links). MIDI CC 31 picks the
footswitch mode: 0 preset, 1 stomp, 2 scenes, 3-127 the next one (CC 29 is the original's: below 64 preset, from 64
stomp). Scenes exist only in waveod's preset format: the stock editor and the
stock firmware do not see them.

## 05 Chain (tab CHAIN)
After the design's "Fluxo de uso 4,3" v2": the 12 places of the chain are always on screen, a grid of 2 rows × 6
slots (empty ones show "+"), each block with its category's icon, the routing drawn as wires from IN to OUT.
Below the grid a bar with the selected block (ON / OFF, category and model with ›, its place, EDIT) or, for the
routing, the three modes, or tempo and volume (BPM / VOL in the top bar: BPM, VOLUME and TAP TEMPO); at the
bottom three parameters of the selection, with ‹ › for the other pages.

| Mode | Signal |
|---|---|
| SÉRIE | Row 1 left to right, then row 2 left to right |
| PARALELO | Row 1 before the split (PRÉ), then lane A (row 1 between the split and the merge ◆) beside lane B (the whole row 2: the wire runs down the corridor between the rows, across row 2 and back up to the merge), mixed, then row 1 after the merge (PÓS) |
| DUAL L/R | PRÉ, then lane A (row 1 from the split to the end) to the left output and row 2 to the right one (no merge) |

- **Routing:** tap IN / OUT or a diamond; the bar shows SÉRIE / PARALELO / DUAL L/R. Drag a diamond sideways:
  it snaps between the slots ("SPLIT ANTES DO SLOT 3"); the merge dragged onto OUT separates the outputs (dual).
- **Split / merge parameters** (the bottom row with a diamond selected): split = BALANÇO A/B (50 = both lanes
  full, towards an end the other lane fades), NÍVEL A, NÍVEL B; merge = MIX A/B (0 = only A, 100 = only B),
  PAN A, PAN B (50 = centre).
- **Add:** tap an empty slot (+): the models screen opens for it.
- **Move:** hold a block 0.28 s, or drag it, onto an empty slot; onto a block the two swap places ("TROCAR DE
  LUGAR COM ..."). The bar below the grid becomes a bin ("ARRASTE ATÉ AQUI PARA EXCLUIR · FORA CANCELA"):
  dropping there deletes the block; dropping elsewhere cancels.
- **Tap** selects, tapping the selected block again edits it; in the bar: ON / OFF, the model = models, EDIT.
| Control | Action |
|---|---|
| MENU turn | Select: IN, then the slots in signal order with the split and the merge where they are |
| Knobs 1-3 | The three parameters shown (a block's, the split's or the merge's) |
| MENU press | Edit the block; an empty slot: choose its effect; IN / a diamond: next mode; tempo and volume: tap tempo |
| MENU hold | Remove the block |
| EFX | Block on / off |
| CTRL | Models: replace the block's effect (an empty slot: choose one) |

The routing is saved with the preset (in bytes the stock firmware fills in itself when it loads a preset: it plays
the same blocks in series). The Split / Path B / Merge modules of a parallel library are ordinary blocks here.
## 03 Edit

A strip with the chain's effects (the edited one ringed), the category and model with ▾, an ON / OFF switch,
and the parameters in rows of three; the active row has a bar in the category's colour. MENU turn picks the
active row (more than six parameters: pages, shown in the top bar), knobs 1-3 change its parameters, EFX
switches the block, CTRL opens the models, MENU press steps to the next row (on a Cab / IR block's mic rows: the MIC
screen), EXIT goes back. Loading another preset (footswitch, MIDI, the editor) closes the edit screen and the ones
about a block (models, MIC, DE ONDE, the destination) back to the screen below them. Holding a parameter, or EXP (the row's
parameter turned last), opens DE ONDE: a scene parameter, a pedal or a footswitch for it (see "DE ONDE"); its
corner names what moves it.

## 06 Models (browse)

Three columns after the design: category (icon and name; scrolls with the finger), type (the library's
subgroup: Clean, Drive...) and model. Tapping a model loads it at once, so it can be heard; ‹ / EXIT keep it.
Opened on an empty slot the first load places the new block there, later ones replace it. Below, the three
parameters of the loaded effect, with ‹ › for the other pages; before anything is loaded, "TOQUE NUM MODELO
PARA ADICIONAR EM ...".

A category lists the original's effects, then waveod's own (built in, then the modules installed from waveod
Studio; docs/blocks.md). A preset block whose module is not installed shows as AUSENTE, keeps its knobs and lets
the signal through until the module is installed again.

| Control | Action |
|---|---|
| MENU turn | Model under the cursor (the whole category; the type follows) |
| MENU press | Load it |
| Knobs 1-3 | The three parameters shown; with nothing loaded yet: category / type / model |

## Save

SAVE opens the save screen with the current preset as destination. "EDITADO" on the home screen (and a `*`
after the preset number in the save screen's title) means the preset has changes that are not saved; loading
another preset drops them.

| Control | Action |
|---|---|
| MENU turn / knob 1 | Destination preset |
| Knob 2, BANK up / down | Destination bank |
| Footswitch A-D | Letter in the destination bank |
| MENU press or SAVE | Save (the sound mutes for a moment) |
| CTRL | Name (keyboard) |
| EFX (or TROCAR) | Swap: the edited preset goes to the destination and stays active there, the destination's preset goes to where it came from |
| EXIT | Cancel |

The screen shows what is stored at the destination now (factory, user or free). After saving, the destination
becomes the active preset and the one the pedal powers up on.

### Name

NOME (or CTRL) opens a keyboard with the preset's name (up to 17 characters: letters, digits, space,
`- . ' /`). Tap the keys; `abc` / `ABC` switches the case, holding APAGAR clears the name. Without touch, MENU
turns move over the keys (knob 1 by rows) and MENU press types the selected one. OK or SAVE keep the name,
EXIT discards it; both return to the save screen, and the name is written with the preset.

## 04 Tuner (tab TUNER)

The TUNER tab, holding any footswitch, or AFINADOR in the settings. The note, its octave, a -50..+50 cent
scale, the frequency and the deviation; green within 3 cents. The reference (A4) and "MUTE" are under the
scale. The output is muted unless "SOM AO AFINAR" says otherwise. Any footswitch, EXIT, MENU or GLOBAL goes
back.

VISOR (GLOBAL › AFINADOR) or a tap on the scale switches between the needle and STROBE: two bands of stripes in
place of the scale that run right when the note is sharp and left when it is flat, 8 px per second per cent (the
lower band at half that speed, for the last cents), and stand still in tune. Kept in waveod's settings.

## Settings (GLOBAL)

Like the original's Global Settings: cards of three by two on two pages (a bar under them shows the page),
each a group of settings with its icon, its name and what it is set to; below them the selected card's
settings in three cells on the knobs, with ‹ › when it has more than three.

| Card | Settings (what MENU press opens) |
|---|---|
| IN / OUT | ENTRADA: VIOLÃO / GUITARRA / LINHA (the analog input stage); NÍVEL IN -20..+20 dB; CAB NAS SAÍDAS: L E R / SÓ EM L / SÓ EM R / NENHUMA (outputs that get the Cab and User IR blocks); NÍVEL AUX IN 0..100. Shared with the stock firmware |
| AFINADOR | AFINADOR (opens the tuner); REFERÊNCIA A4 430..450 Hz; SOM AO AFINAR: MUDO / LIGADO; VISOR: AGULHA / STROBE (the tuner) |
| ÁUDIO USB | CANAIS: 6 IN / 4 OUT (with the MIDI port) or 2 IN / 2 OUT, the computer sees the pedal again; NÍVEL GRAV., RETORNO DO PC, GRAVAÇÃO 1 / 2, AUX NA GRAV. (USB audio levels and what channels 1 / 2 record, docs/architecture.md, USB) |
| PEDAIS EXP | EXP 1, EXP 2: the calibrated range, the bar where the pedal is now (a tap opens the calibration of that pedal); CALIBRAR (the calibration, below) |
| EQ GLOBAL | EQ GLOBAL on / off; BANDAS (the EQ, below) |
| EXP 2 / FS EXT. | JACK EXP 2: PEDAL / 1 FOOTSWITCH / 2 FOOTSWITCHES (as a footswitch input EXP 2 no longer moves its targets); FS EXT. 1 / 2: function of the jack's footswitches (same list as the preset footswitches), run when released |
| MIDI | ENTRADA: SÓ TRS / SÓ USB / TRS E USB (where program and control changes are taken from); CANAL DO TRS / DO USB: OMNI / 1..16; FONTE DO TEMPO: INTERNO (taps, knobs) / CLOCK DO TRS / CLOCK DO USB / CLOCK EXTERNO (only the MIDI clock) / INTERNO E CLOCK; ENVIAR CLOCK: DESLIGADO / TRS / USB / TRS E USB (MIDI clock at the running tempo) |
| PRESETS | TEMPO GLOBAL: DESLIGADO / 40..300 BPM, when on the effects follow it instead of each preset's tempo (shared with the stock firmware); RASTRO NA TROCA: DESLIGADO / ATÉ 2 s / 5 s / 10 s (factory) / 20 s, the delays and reverbs of the preset that was playing ring out after a preset change for at most that long (docs/spillover.md); KNOBS NA CENA: AUTO (factory: a parameter turned while the scenes are in play gets a value per scene) / MANUAL (only those marked UM VALOR POR CENA), see Scenes; MOTOR CAB / IR: WAVEOD / ORIGINAL, the engine of the Cab and User IR blocks, the preset reloads (docs/cab.md) |
| CONTROLES | LIGAÇÕES: opens CONTROLES on the global table (see "Global controls" above) |
| TELA | BRILHO 10..100 %; MARCA DE TOQUE: marker and coordinates under the finger |
| LATÊNCIA | TESTE: opens the latency test, with the last result |
| SISTEMA | DIAGNÓSTICO: the panel (clock and the bootloader's, DSP load, watchdog, the last crash, touch, USB, MIDI, flash controller and last flash error); PROTEÇÃO: a knob picks FALHA / TRAVAR / TRAVAR SEM IRQ, a tap on it runs it: the pedal restarts and tells what happened (saves the settings first; unsaved preset edits are lost); VERSÃO: git revision of the build |

MENU turns pick a card (the page follows), knobs 1-3 change its three settings shown, MENU press opens what
the card opens or shows its next three settings, EXIT or GLOBAL go back. By touch: tap a card to select it, tap
it again = MENU press; swipe the cards sideways for the other page; tap a setting to open / step it, drag it
sideways to change it; ‹ › = the card's other settings. Changes apply at once and are written to the flash when
the screen is left (the stock firmware's settings record and waveod's own, see docs/architecture.md).

### Expression pedal calibration

PEDAIS EXP › CALIBRAR, the original's steps with the pedal drawn as it moves: EXP 1 / EXP 2 on top, the pedal
(its treadle at the angle the pedal is at, the toe switch lit red while it is pressed), the steps with their
values and what to do, and below the pedal's whole travel (0..255) with the calibrated range, where it is now
(white) and the toe switch threshold (red).

1. CALIBRAR: lift the pedal all the way to the heel, PRÓXIMO;
2. push it all the way to the toe without pressing the switch, PRÓXIMO (SALVAR on EXP 2); a range under 32 starts
   again from the heel;
3. EXP 1: press hard on the toe, through the switch, release, SALVAR. The threshold is three quarters of the way
   from the toe to the strongest press; with no press past the toe (+8) the old threshold stays, or none (–) when
   it falls inside the new range.

Nothing changes until SALVAR; EXIT during the steps leaves the calibration as it was. While the screen is shown the
pedals move nothing (no parameter, no A / B on the toe switch). MENU press = the button, MENU turns = EXP 1 /
EXP 2, knobs 1-3 = heel, toe and toe switch threshold one step at a time (at once), EXIT or GLOBAL = back; by
touch the button and EXP 1 / EXP 2. The values are written to the flash when the screen is left.
### Global EQ

The stock firmware's EQ after the effect chain: low cut, low shelf, bands 1-4, high shelf, high cut and the
level. The response on a graph from 20 Hz to 20 kHz and ±18 dB (lines at 50 Hz-10 kHz and ±6 / ±12 dB, the area
under the curve filled), with a handle per stage at its frequency and gain (a cut on the curve): L, LS, 1-4,
HS, H; amber for the selected stage, hollow when it is off. The level is in the graph's corner.

Behind the curve, FFT (on by default): the spectrum of what goes through the EQ, its input as a grey line and its
output as a grey area, from 0 dBFS at the top to -90 at the bottom (the edit screen's analyser: 4096 points, rising
at once and falling 30 dB per second). FLAT (hold it): every gain to 0 dB, both cuts off and the level to 50, the
frequencies, Qs and switches kept; it then reads VOLTAR, and a tap puts the EQ back as it was (until the next
change).

| Control | Action |
|---|---|
| MENU turn | Stage (L.CUT, L.SHF, B1-B4, H.SHF, H.CUT, NÍVEL) |
| Knobs 1-3 | Frequency (accelerated) / Q / gain; the level on NÍVEL |
| MENU press | Stage on / off |
| EFX | Whole EQ on / off |
| CTRL | FFT on / off |
| EXIT, GLOBAL | Back |

Touch: tap a stage or its handle to select it, hold a stage to switch it; drag a handle: sideways = frequency,
up / down = gain (a stage that is off comes on; the low cut at the far left and the high cut at the far right
are off); tap FFT / EQ LIGADO / ON-OFF in the head, hold FLAT (VOLTAR: tap); drag a value sideways to turn its knob. The low cut at 19 Hz and the high cut at 20001 Hz read OFF, as on the stock editor.

## Touch

The display has a capacitive touch panel. Every touch action is a shortcut for a control above, so the pedal
works the same without it.

| Where | Gesture | Action |
|---|---|---|
| Top bar | Tap a tab | Show it |
| | Tap ‹ | Back (EXIT) |
| | Swipe the preset left / right, tap it | Next / previous preset, home screen |
| | Tap BPM / VOL | Tempo and volume on the chain screen (again = back to the block) |
| | Tap SAVE | Save screen |
| 01 Preset, blocks | Tap | Edit the block |
| | Hold | Block on / off (bar along the bottom) |
| 01 Preset, preset name | Swipe left / right | Next / previous preset |
| | Swipe up / down | Bank up / down |
| 01 Preset, quick knobs | Drag sideways or up / down | Turn the knob (200 px = whole range, right / up = more) |
| 02 Stomp, cards | Tap | Edit the block |
| | Hold | Block on / off |
| 05 Chain, slots | Tap a block | Select; tap the selected block again = edit |
| | Tap an empty slot | Choose its effect |
| | Hold or drag a block, drop | Move (onto a block: swap); on the bar below the grid = delete; elsewhere = cancel |
| 05 Chain, IN / OUT, diamonds | Tap | Routing modes in the bar; a diamond: its parameters |
| | Drag a diamond sideways | Move the split / merge; the merge onto OUT = dual L/R |
| 05 Chain, bar | Tap ON-OFF / the model / EDIT | On-off / models / edit |
| | Tap TAP TEMPO (tempo and volume) | Tap tempo |
| 05 Chain, parameters | Drag sideways or up / down | Change it; ‹ › = other pages |
| 03 Edit, strip | Tap a block | Edit it instead |
| | Hold | That block on / off |
| 03 Edit, name row | Tap the model name / ON-OFF | Models / on-off |
| 03 Edit, parameters | Tap | Make the row active |
| | Drag sideways | Turn the knob |
| | Swipe up / down | Next / previous page |
| 06 Models | Tap a category / a type | Show it |
| | Tap a model | Load it (heard at once) |
| | Drag a column up / down | Scroll |
| | Drag a parameter sideways or up / down | Change it; ‹ › = other pages |
| Save | Tap SALVAR / NOME / CANCELAR, the < > buttons | Save, name, cancel, previous / next destination |
| Name | Tap the keys, hold APAGAR | Type, clear the name |
| | Swipe sideways / up-down | Previous / next destination, bank |

The touch marker and the diagnostics are in the settings. Deleting a block by touch needs it carried to the bin,
so a stray touch cannot delete anything.

## Not decided yet

The encoder direction is assumed (clockwise = up); it is a
one-line change in `src/board/qme200/controls.c` if the pedal shows otherwise. The touch coordinates are used
as the controller reports them, like the stock firmware does; if the marker of GLOBAL does not follow the
finger, `SWAP_XY` / `MIRROR_X` / `MIRROR_Y` in `src/board/qme200/touch_gt911.c` fix it.
