/* code/data stop below the 2K software stack.  bitmap $A000-$BFFF and
 * screen matrix $C000-$C3FF are reserved, not invisible linker headroom. */
__basic_zp_start = 0x0002;
__basic_zp_end = 0x0090;
MEMORY { ram (rw) : ORIGIN = 0x2001, LENGTH = 0x77ff }
INCLUDE commodore.ld
INPUT(unmap-basic.o save-basic.o)
__stack = 0xa000;
OUTPUT_FORMAT { SHORT(0x2001) TRIM(ram) }
