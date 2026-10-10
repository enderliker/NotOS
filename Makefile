CC := gcc
LD := ld
CFLAGS := -m32 -std=c11 -Os -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
.PHONY: all clean run test
all: build/notos.img
build:
	mkdir -p build
build/boot.o: boot/boot.S | build
	$(CC) -m32 -c $< -o $@
build/boot.bin: build/boot.o boot/link.ld
	$(LD) -m elf_i386 -T boot/link.ld --oformat binary $< -o $@
build/entry.o: kernel/entry.S | build
	$(CC) -m32 -c $< -o $@
build/kernel.o: kernel/kernel.c kernel/base.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/gui.o: kernel/gui.c kernel/base.h kernel/font.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/kernel.elf: build/entry.o build/kernel.o build/gui.o kernel/link.ld
	$(LD) -m elf_i386 -T kernel/link.ld build/entry.o build/kernel.o build/gui.o -o $@
build/kernel.bin: build/kernel.elf
	objcopy -O binary $< $@
build/notos.img: build/boot.bin build/kernel.bin tools/image.py
	python3 tools/image.py
run: all
	qemu-system-i386 -drive format=raw,file=build/notos.img -serial stdio
test: all
	python3 tests/smoke.py
clean:
	rm -rf build
