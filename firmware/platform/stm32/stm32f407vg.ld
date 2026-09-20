ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_stack_limit = _estack - 4096;
SECTIONS {
    .isr_vector : { . = ALIGN(512); KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(8); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > RAM
    .msp_stack _stack_limit (NOLOAD) : { . += 4096; } > RAM
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(_ebss <= _stack_limit, "Static RAM overlaps reserved MSP stack")
ASSERT(SIZEOF(.isr_vector) == 392, "STM32F407 requires 98 vector entries")
