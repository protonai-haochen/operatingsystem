ENTRY(_start)

SECTIONS
{
    /* Kernel is loaded at 1 MiB */
    . = 0x00100000;

    /* Entry stub goes here */
    .entry ALIGN(4K) : {
        *(.entry)
    }

    /* All other code follows */
    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        *(.bss*)
        *(COMMON)
    }
}
