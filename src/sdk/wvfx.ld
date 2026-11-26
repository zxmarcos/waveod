/* A block of waveod's built as a module (tools/wvfx.py): the four regions of the original's modules
   (engine/module_loader.h). A block keeps its state in the memory the host gives it, so only code and
   constants are expected; tools/wvfx.py refuses writable sections. */
ENTRY(wvfx_module_header)

SECTIONS
{
  ER_IROM 0x00000000 : ALIGN(8)
  {
    KEEP(*(.module_header))
    *(.text .text.*)
  }

  ER_SROM 0x10000000 : ALIGN(8)
  {
    *(.rodata .rodata.*)
    LONG(0)
  }

  ER_SRAM 0x30000000 (NOLOAD) : ALIGN(8)
  {
    *(.bss.sram .bss.sram.*)
  }

  ER_IRAM 0x20000000 : ALIGN(8)
  {
    *(.data .data.*)
    *(.bss .bss.* COMMON)
  }

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.init_array*) *(.fini_array*) }
}
