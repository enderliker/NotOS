CC := gcc
LD := ld
CFLAGS := -m32 -march=i486 -mno-sse -mno-sse2 -mno-mmx -msoft-float -std=c11 -Os -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
.PHONY: all clean run test test-online
all: build/notos.img
build:
	mkdir -p build
build/boot.o: boot/boot.S | build
	$(CC) -m32 -c $< -o $@
build/boot.bin: build/boot.o boot/link.ld
	$(LD) -m elf_i386 -T boot/link.ld --oformat binary $< -o $@
build/entry.o: kernel/entry.S | build
	$(CC) -m32 -c $< -o $@
KERNEL_OBJS := build/entry.o build/isr.o build/user_entry.o build/user_program.o $(patsubst kernel/%.c,build/%.o,$(wildcard kernel/*.c))
build/user_entry.o: kernel/user_entry.S | build
	$(CC) -m32 -c $< -o $@
build/user_program.o: kernel/user_program.S kernel/syscall_numbers.h | build
	$(CC) -m32 -c $< -o $@
build/isr.o: kernel/isr.S | build
	$(CC) -m32 -c $< -o $@
build/%.o: kernel/%.c $(wildcard kernel/*.h) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/kernel.elf: $(KERNEL_OBJS) kernel/link.ld
	$(LD) -m elf_i386 -T kernel/link.ld $(KERNEL_OBJS) -o $@
build/kernel.bin: build/kernel.elf
	objcopy -O binary $< $@
build/notos.img: build/boot.bin build/kernel.bin tools/image.py
	python3 tools/image.py
run: all
	qemu-system-i386 -drive format=raw,file=build/notos.img -serial stdio -netdev user,id=net0 -device rtl8139,netdev=net0
build/lang-unit: tests/lang_unit.c kernel/lang.c $(wildcard kernel/*.h) | build
	gcc -std=c11 -Wall -Wextra -Werror tests/lang_unit.c kernel/lang.c -o $@
build/net-unit: tests/net_unit.c kernel/net.c $(wildcard kernel/*.h) | build
	gcc -std=c11 -Wall -Wextra -Werror tests/net_unit.c -o $@
build/clock-unit: tests/clock_unit.c kernel/clock_math.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/clock_unit.c -o $@
build/physical-unit: tests/physical_unit.c kernel/physical_core.c kernel/physical_core.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/physical_unit.c kernel/physical_core.c -o $@
build/heap-unit: tests/heap_unit.c kernel/heap_core.c kernel/heap_core.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/heap_unit.c kernel/heap_core.c -o $@
build/user-unit: tests/user_unit.c kernel/user_memory.c kernel/user_memory.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/user_unit.c kernel/user_memory.c -o $@
test: all build/lang-unit build/net-unit build/clock-unit build/physical-unit build/heap-unit build/user-unit
	./build/lang-unit
	./build/net-unit
	./build/clock-unit
	./build/physical-unit
	./build/heap-unit
	./build/user-unit
	python3 tests/smoke.py
	python3 tests/exceptions.py
	python3 tests/interrupts.py
	python3 tests/physical.py
	python3 tests/paging.py
	python3 tests/heap.py
	python3 tests/user.py
	python3 tests/network.py
test-online: all
	python3 tests/online.py
clean:
	rm -rf build
