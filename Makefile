CC := gcc
LD := ld
CFLAGS := -m32 -march=i386 -mno-sse -mno-sse2 -mno-mmx -msoft-float -std=c11 -Os -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
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
KERNEL_OBJS := build/entry.o $(patsubst kernel/%.c,build/%.o,$(wildcard kernel/*.c))
build/%.o: kernel/%.c $(wildcard kernel/*.h) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/kernel.elf: $(KERNEL_OBJS) kernel/link.ld
	$(LD) -m elf_i386 -T kernel/link.ld $(KERNEL_OBJS) -o $@
build/kernel.bin: build/kernel.elf
	objcopy -O binary $< $@
build/notos.img: build/boot.bin build/kernel.bin tools/image.py
	python3 tools/image.py
run: all
	qemu-system-i386 -drive format=raw,file=build/notos.img -serial stdio
build/lang-unit: tests/lang_unit.c kernel/lang.c $(wildcard kernel/*.h) | build
	gcc -std=c11 -Wall -Wextra -Werror tests/lang_unit.c kernel/lang.c -o $@
test: all build/lang-unit
	./build/lang-unit
	python3 tests/smoke.py
clean:
	rm -rf build
