/*
 * Memory map of waveod on the QME-200 (i.MX RT1062, FlexRAM as set by the bootloader: ITCM 480 KB,
 * DTCM 32 KB, OCRAM 512 KB; SDRAM 32 MB).
 *
 *   ITCM   0x00000000  vectors, application code and constants (fast; not LVGL and the screens)  until 0x58000
 *          0x00058000  effect module code pool (128 KB, engine/pools.c)
 *   DTCM   0x20000000  stack (32 KB, MSP = 0x20008000 like the original)
 *   OCRAM  0x20200000  audio DMA buffers (16 KB, non-cacheable)
 *          0x20204000  effect module state pool (192 KB)
 *          0x20234000  reserved: RAM words of the original app that modules use (engine/host_abi.h)
 *          0x20242000  application .bss (216 KB)
 *          0x20278000  USB controller memory (32 KB, non-cacheable)
 *   SDRAM  0x80000000  code and initialised data, the drum machine's pack (filled by tools/mkfw.py from the base
 *                      firmware), LVGL and fonts' .bss, LVGL heap, then the looper buffer (15.5 MB, not cleared)
 *                      until 0x81300000
 *          0x81301000  module constant pool, 0x81321000 module buffer pool (engine/pools.c)
 *          0x81F00000  two frame buffers (write-through)
 *
 * The bootloader loads the two images (ITCM and SDRAM) and jumps to the reset vector; everything else is
 * NOLOAD and cleared by the start-up code.
 */
MEMORY
{
  ITCM      (rwx) : ORIGIN = 0x00000000, LENGTH = 0x00058000
  DTCM      (rw)  : ORIGIN = 0x20000000, LENGTH = 0x00008000
  OCRAM_DMA (rw)  : ORIGIN = 0x20200000, LENGTH = 0x00004000
  HOST_ABI  (rw)  : ORIGIN = 0x20234000, LENGTH = 0x0000E000
  OCRAM     (rw)  : ORIGIN = 0x20242000, LENGTH = 0x00036000
  OCRAM_USB (rw)  : ORIGIN = 0x20278000, LENGTH = 0x00008000
  SDRAM     (rwx) : ORIGIN = 0x80000000, LENGTH = 0x01300000
}

ENTRY(reset_handler)

SECTIONS
{
  .itcm :
  {
    KEEP(*(.vectors))
    . = 0x470;                              /* the original reset handler address */
    KEEP(*(.reset_fixed))
    *(EXCLUDE_FILE(*liblvgl.a:*) .text .text.*)
    *(EXCLUDE_FILE(*liblvgl.a:*) .rodata .rodata.*)
    . = ALIGN(4);
  } > ITCM

  .sdram :
  {
    *(.sdram_const .sdram_const.*)
    *(.sdram_text .sdram_text.*)            /* code marked by ui/sdram_code.h (the screens) */
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.data .data.*)
    . = ALIGN(4);
  } > SDRAM

  /* written into the SDRAM image by tools/mkfw.py (tools/drumpack.py), so the bootloader loads it */
  .drum_pack (NOLOAD) :
  {
    . = ALIGN(32);
    __drum_pack_start = .;
    . = . + 0xB0000;
    __drum_pack_end = .;
  } > SDRAM

  .sdram_bss (NOLOAD) :
  {
    . = ALIGN(8);
    __sdram_bss_start = .;
    *liblvgl.a:*(.bss .bss.* COMMON)
    *(.sdram_bss .sdram_bss.*)                /* buffers marked for the SDRAM (editor/debug_msgs.c) */
    . = ALIGN(4);
    __sdram_bss_end = .;
  } > SDRAM

  .looper (NOLOAD) :
  {
    . = ALIGN(32);
    *(.looper)
  } > SDRAM

  .dma_buffers (NOLOAD) :
  {
    *(.dma_buffers)
  } > OCRAM_DMA

  .usb_dma (NOLOAD) :
  {
    *(.usb_dma)
  } > OCRAM_USB

  .host_abi (NOLOAD) :
  {
    __host_abi_start = .;
    . = . + LENGTH(HOST_ABI);
    __host_abi_end = .;
  } > HOST_ABI

  .bss (NOLOAD) :
  {
    . = ALIGN(8);
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > OCRAM

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.ARM.attributes) }
}
