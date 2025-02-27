# -----------------------------------------------------------------------
# Project Information
# -----------------------------------------------------------------------

PROJECT_IDX	= 1

# -----------------------------------------------------------------------
# Host Linux Variables
# -----------------------------------------------------------------------

SHELL       = /bin/sh
DISK        = /dev/sdb
TTYUSB1     = /dev/ttyUSB1
DIR_OSLAB   = $(HOME)/OSLab-RISC-V
DIR_QEMU    = $(DIR_OSLAB)/qemu
DIR_UBOOT   = $(DIR_OSLAB)/u-boot

# -----------------------------------------------------------------------
# Build and Debug Tools
# -----------------------------------------------------------------------

HOST_CC         = gcc
CROSS_PREFIX    = riscv64-unknown-linux-gnu-
CC              = $(CROSS_PREFIX)gcc
OBJDUMP         = $(CROSS_PREFIX)objdump
GDB             = $(CROSS_PREFIX)gdb
QEMU            = $(DIR_QEMU)/riscv64-softmmu/qemu-system-riscv64
UBOOT           = $(DIR_UBOOT)/u-boot
MINICOM         = minicom

# -----------------------------------------------------------------------
# Build/Debug Flags and Variables
# -----------------------------------------------------------------------

CFLAGS          = -O0 -fno-builtin -nostdlib -nostdinc -Wall -mcmodel=medany -ggdb3

BOOT_INCLUDE    = -I$(DIR_ARCH)/include
BOOT_CFLAGS     = $(CFLAGS) $(BOOT_INCLUDE) -Wl,--defsym=TEXT_START=$(BOOTLOADER_ENTRYPOINT) -T riscv.lds

DECOMPRESS_INCLUDE  = -I$(DIR_DEFLATE) -I$(DIR_ARCH)/include -Iinclude
DECOMPRESS_CFLAGS   = $(CFLAGS) $(DECOMPRESS_INCLUDE) -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds

KERNEL_INCLUDE  = -I$(DIR_ARCH)/include -Iinclude
KERNEL_CFLAGS   = $(CFLAGS) $(KERNEL_INCLUDE) -Wl,--defsym=TEXT_START=$(KERNEL_ENTRYPOINT) -T riscv.lds

USER_INCLUDE    = -I$(DIR_TINYLIBC)/include
USER_CFLAGS     = $(CFLAGS) $(USER_INCLUDE)

QEMU_OPTS       = -nographic -machine virt -m 256M -kernel $(UBOOT) -bios none \
                     -drive if=none,format=raw,id=image,file=${ELF_IMAGE} \
                     -device virtio-blk-device,drive=image \
                     -monitor telnet::45454,server,nowait -serial mon:stdio
QEMU_DEBUG_OPT  = -s -S

# -----------------------------------------------------------------------
# UCAS-OS Entrypoints and Variables
# -----------------------------------------------------------------------

DIR_ARCH        = ./arch/riscv
DIR_BUILD       = ./build
DIR_INIT        = ./init
DIR_KERNEL      = ./kernel
DIR_LIBS        = ./libs
DIR_TINYLIBC    = ./tiny_libc
DIR_TEST        = ./test
DIR_TEST_PROJ   = $(DIR_TEST)/test_project$(PROJECT_IDX)
DIR_DEFLATE     = ./tools/deflate
DIR_DECOMPRESS     = ./decompress

BOOTLOADER_ENTRYPOINT   = 0x50200000
KERNEL_ENTRYPOINT       = 0x50201000
USER_ENTRYPOINT         = 0x52000000

# -----------------------------------------------------------------------
# UCAS-OS Kernel Source Files
# -----------------------------------------------------------------------

SRC_BOOT    = $(wildcard $(DIR_ARCH)/boot/*.S)
SRC_ARCH    = $(wildcard $(DIR_ARCH)/kernel/*.S)
SRC_BIOS    = $(wildcard $(DIR_ARCH)/bios/*.c)
SRC_INIT    = $(wildcard $(DIR_INIT)/*.c)
SRC_KERNEL  = $(wildcard $(DIR_KERNEL)/*/*.c)
SRC_LIBS    = $(wildcard $(DIR_LIBS)/*.c)
SRC_DECOMPRESS_1 = $(wildcard $(DIR_DECOMPRESS)/*.c)
SRC_DECOMPRESS_2 = $(wildcard $(DIR_DECOMPRESS)/*.S)
SRC_DEFLATE_1 = $(wildcard $(DIR_DEFLATE)/*.c)
SRC_DEFLATE_2 = $(wildcard $(DIR_DEFLATE)/lib/*.c)

SRC_MAIN    = $(SRC_ARCH) $(SRC_INIT) $(SRC_BIOS) $(SRC_KERNEL) $(SRC_LIBS)
SRC_DECOMPRESS= $(SRC_DECOMPRESS_1) $(SRC_DECOMPRESS_2) $(SRC_DEFLATE_1) $(SRC_DEFLATE_2) $(SRC_BIOS) $(SRC_LIBS)

ELF_BOOT    = $(DIR_BUILD)/bootblock
ELF_MAIN    = $(DIR_BUILD)/main
ELF_IMAGE   = $(DIR_BUILD)/image
ELF_DECOMPRESS    = $(DIR_BUILD)/decompress

# -----------------------------------------------------------------------
# UCAS-OS User Source Files
# -----------------------------------------------------------------------

SRC_CRT0    = $(wildcard $(DIR_ARCH)/crt0/*.S)
OBJ_CRT0    = $(DIR_BUILD)/$(notdir $(SRC_CRT0:.S=.o))

SRC_USER    = $(wildcard $(DIR_TEST_PROJ)/*.c)
ELF_USER    = $(patsubst %.c, %, $(foreach file, $(SRC_USER), $(DIR_BUILD)/$(notdir $(file))))

# -----------------------------------------------------------------------
# Host Linux Tools Source Files
# -----------------------------------------------------------------------

SRC_CREATEIMAGE = ./tools/createimage.c
ELF_CREATEIMAGE = $(DIR_BUILD)/$(notdir $(SRC_CREATEIMAGE:.c=))

# -----------------------------------------------------------------------
# Top-level Rules
# -----------------------------------------------------------------------

all: dirs elf image asm # floppy

dirs:
	@mkdir -p $(DIR_BUILD)

clean:
	rm -rf $(DIR_BUILD)

floppy:
	sudo fdisk -l $(DISK)
	sudo dd if=$(DIR_BUILD)/image of=$(DISK)3 conv=notrunc

asm: $(ELF_BOOT) $(ELF_DECOMPRESS) $(ELF_MAIN) $(ELF_USER)
	for elffile in $^; do $(OBJDUMP) -d $$elffile > $(notdir $$elffile).txt; done

gdb:
	$(GDB) $(ELF_BOOT) $(ELF_MAIN) -ex "target remote:1234"

run:
	$(QEMU) $(QEMU_OPTS)

debug:
	$(QEMU) $(QEMU_OPTS) $(QEMU_DEBUG_OPT)

minicom:
	sudo $(MINICOM) -D $(TTYUSB1)

.PHONY: all dirs clean floppy asm gdb run debug minicom

# -----------------------------------------------------------------------
# UCAS-OS Rules
# -----------------------------------------------------------------------

$(ELF_BOOT): $(SRC_BOOT) riscv.lds
	$(CC) $(BOOT_CFLAGS) -o $@ $(SRC_BOOT) -e main

$(ELF_MAIN): $(SRC_MAIN) riscv.lds
	$(CC) $(KERNEL_CFLAGS) -o $@ $(SRC_MAIN)

$(ELF_DECOMPRESS): $(SRC_DECOMPRESS) riscv.lds
	$(CC) $(DECOMPRESS_CFLAGS) -DFREESTANDING -o $@ $(SRC_DECOMPRESS) 

$(OBJ_CRT0): $(SRC_CRT0)
	$(CC) $(USER_CFLAGS) -I$(DIR_ARCH)/include -c $< -o $@

$(DIR_BUILD)/%: $(DIR_TEST_PROJ)/%.c $(OBJ_CRT0) riscv.lds
	$(CC) $(USER_CFLAGS) -o $@ $(OBJ_CRT0) $< -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds
	$(eval USER_ENTRYPOINT := $(shell python3 -c "print(hex(int('$(USER_ENTRYPOINT)', 16) + int('0x10000', 16)))"))

elf: $(ELF_BOOT) $(ELF_DECOMPRESS) $(ELF_MAIN) $(ELF_USER)

.PHONY: elf

# -----------------------------------------------------------------------
# Host Linux Rules
# -----------------------------------------------------------------------

$(ELF_CREATEIMAGE): $(SRC_CREATEIMAGE) $(SRC_DEFLATE_1) $(SRC_DEFLATE_2)
	$(HOST_CC) $(SRC_CREATEIMAGE) $(SRC_DEFLATE_1) $(SRC_DEFLATE_2) -o $@ -ggdb -Wall -I$(DIR_DEFLATE) -I$(DIR_ARCH)/include -DFREESTANDING

image: $(ELF_CREATEIMAGE) $(ELF_BOOT) $(ELF_DECOMPRESS) $(ELF_MAIN) $(ELF_USER)
	cd $(DIR_BUILD) && ./$(<F) --extended $(filter-out $(<F), $(^F))

.PHONY: image
