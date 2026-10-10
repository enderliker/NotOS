CC := gcc
LD := ld
BEARSSL_INC := build/bearssl/bearssl-0.6/inc
CFLAGS := -m32 -march=i486 -mno-sse -mno-sse2 -mno-mmx -msoft-float -std=c11 -Os -fno-builtin -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
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
KERNEL_OBJS := build/entry.o build/isr.o build/user_entry.o build/user_program.o build/notlang_blob.o $(patsubst kernel/%.c,build/%.o,$(wildcard kernel/*.c))
build/notlang_vm.o: user/notlang_vm.c kernel/notlang_bytecode.h kernel/syscall_numbers.h kernel/base.h | build
	$(CC) $(CFLAGS) -ffunction-sections -c $< -o $@
build/notlang_vm.elf: build/notlang_vm.o user/link.ld
	$(LD) -m elf_i386 -T user/link.ld $< -o $@
build/notlang_vm.bin: build/notlang_vm.elf
	objcopy -O binary $< $@
build/notlang_blob.o: build/notlang_vm.bin
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.rodata.uservm,alloc,load,readonly,data,contents --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly $< $@
build/user_entry.o: kernel/user_entry.S | build
	$(CC) -m32 -c $< -o $@
build/user_program.o: kernel/user_program.S kernel/syscall_numbers.h kernel/user_test_abi.h | build
	$(CC) -m32 -c $< -o $@
build/isr.o: kernel/isr.S | build
	$(CC) -m32 -c $< -o $@
build/%.o: kernel/%.c $(wildcard kernel/*.h) | build
	$(CC) $(CFLAGS) -Ikernel/freestanding -I$(BEARSSL_INC) -c $< -o $@
build/bearssl.ready: tools/fetch_bearssl.py | build
	python3 tools/fetch_bearssl.py
build/libbearssl.a: build/bearssl.ready tools/build_bearssl.py
	CC="$(CC)" NOTOS_CFLAGS="$(CFLAGS)" python3 tools/build_bearssl.py
build/trust_roots.h: trust/roots.pem tools/trust_anchors.py | build
	python3 tools/trust_anchors.py $< $@
build/tls.o: build/trust_roots.h build/bearssl.ready
build/tls_profile.o build/entropy.o build/tls_selftest.o build/x509_policy.o: build/bearssl.ready
build/kernel.elf: $(KERNEL_OBJS) kernel/link.ld build/libbearssl.a
	$(LD) -m elf_i386 -T kernel/link.ld $(KERNEL_OBJS) build/libbearssl.a -o $@
build/kernel.bin: build/kernel.elf
	objcopy -O binary $< $@
build/notos.img: build/boot.bin build/kernel.bin tools/image.py
	python3 tools/image.py
build/tls-fixtures.ready: tools/tls_fixtures.py | build
	python3 tools/tls_fixtures.py
build/test_trust_roots.h: build/tls-fixtures.ready tools/trust_anchors.py
	python3 tools/trust_anchors.py build/tls-fixtures/ca.pem $@
build/tls_test.o: kernel/tls.c $(wildcard kernel/*.h) build/test_trust_roots.h build/bearssl.ready
	$(CC) $(CFLAGS) -Ikernel/freestanding -I$(BEARSSL_INC) -DNOTOS_TRUST_HEADER='"../build/test_trust_roots.h"' -c $< -o $@
build/kernel-tls-test.elf: $(filter-out build/tls.o,$(KERNEL_OBJS)) build/tls_test.o kernel/link.ld build/libbearssl.a
	$(LD) -m elf_i386 -T kernel/link.ld $(filter-out build/tls.o,$(KERNEL_OBJS)) build/tls_test.o build/libbearssl.a -o $@
build/kernel-tls-test.bin: build/kernel-tls-test.elf
	objcopy -O binary $< $@
build/notos-tls-test.img: build/boot.bin build/kernel-tls-test.bin tools/image.py
	python3 tools/image.py build/kernel-tls-test.bin $@
run: all
	python3 tools/run.py
build/lang-unit: tests/lang_unit.c kernel/lang.c $(wildcard kernel/*.h) | build
	gcc -std=c11 -Wall -Wextra -Werror tests/lang_unit.c kernel/lang.c -o $@
build/net-unit: tests/net_unit.c kernel/net.c kernel/tcp_retry.c $(wildcard kernel/*.h) | build
	gcc -std=c11 -Wall -Wextra -Werror tests/net_unit.c kernel/tcp_retry.c -o $@
build/clock-unit: tests/clock_unit.c kernel/clock_math.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/clock_unit.c -o $@
build/physical-unit: tests/physical_unit.c kernel/physical_core.c kernel/physical_core.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/physical_unit.c kernel/physical_core.c -o $@
build/heap-unit: tests/heap_unit.c kernel/heap_core.c kernel/heap_core.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/heap_unit.c kernel/heap_core.c -o $@
build/user-unit: tests/user_unit.c kernel/user_memory.c kernel/user_memory.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/user_unit.c kernel/user_memory.c -o $@
build/scheduler-unit: tests/scheduler_unit.c kernel/scheduler_policy.c kernel/scheduler_policy.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/scheduler_unit.c kernel/scheduler_policy.c -o $@
build/mouse-unit: tests/mouse_unit.c kernel/mouse_decode.c kernel/mouse_decode.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/mouse_unit.c kernel/mouse_decode.c -o $@
build/notlang-unit: tests/notlang_unit.c kernel/notlang_compile.c kernel/notlang_bytecode.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/notlang_unit.c kernel/notlang_compile.c -o $@
build/utc-unit: tests/utc_unit.c kernel/utc.c kernel/platform_tls.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/utc_unit.c kernel/utc.c -o $@
build/certificate-unit: tests/certificate_unit.c kernel/certificate_policy.c kernel/certificate_policy.h build/tls-fixtures.ready | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/certificate_unit.c kernel/certificate_policy.c -o $@
test: all build/notos-tls-test.img build/certificate-unit build/utc-unit build/notlang-unit build/mouse-unit build/lang-unit build/net-unit build/clock-unit build/physical-unit build/heap-unit build/user-unit build/scheduler-unit
	./build/certificate-unit
	./build/utc-unit
	./build/notlang-unit
	./build/mouse-unit
	./build/lang-unit
	./build/net-unit
	./build/clock-unit
	./build/physical-unit
	./build/heap-unit
	./build/user-unit
	./build/scheduler-unit
	python3 tests/smoke.py
	python3 tests/exceptions.py
	python3 tests/interrupts.py
	python3 tests/physical.py
	python3 tests/paging.py
	python3 tests/heap.py
	python3 tests/user.py
	python3 tests/multitask.py
	python3 tests/notlang.py
	python3 tests/mouse.py
	python3 tests/process.py
	python3 tests/storage.py
	python3 tests/tls_platform.py
	python3 tests/https.py
	python3 tests/network.py
test-online: all
	python3 tests/online.py
	python3 tests/https_online.py
clean:
	rm -rf build
