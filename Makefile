CC := gcc
LD := ld
BEARSSL_INC := build/bearssl/bearssl-0.6/inc
CFLAGS := -m32 -march=i486 -mno-sse -mno-sse2 -mno-mmx -msoft-float -std=c11 -Os -ffunction-sections -fdata-sections -fno-builtin -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
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
ELF_DEMOS := demo nx-heap nx-stack stack-guard readonly sleep
ELF_DEMO_OBJS := $(addprefix build/elf-,$(addsuffix -blob.o,$(ELF_DEMOS)))
USER_SHELL_DEFAULT ?= 0
KERNEL_OBJS := build/system-ca.o build/curl-blob.o build/socket-demo-blob.o build/libc-demo-blob.o build/sh-blob.o $(ELF_DEMO_OBJS) build/entry.o build/isr.o build/user_entry.o build/user_program.o build/notlang_blob.o $(patsubst kernel/%.c,build/%.o,$(wildcard kernel/*.c))
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
	$(CC) $(CFLAGS) -DUSER_SHELL_DEFAULT=$(USER_SHELL_DEFAULT) -Ikernel/freestanding -I$(BEARSSL_INC) $(MBEDTLS_FLAGS) $(LWIP_FLAGS) -c $< -o $@
build/bearssl.ready: tools/fetch_bearssl.py | build
	python3 tools/fetch_bearssl.py
build/libbearssl.a: build/bearssl.ready tools/build_bearssl.py
	CC="$(CC)" NOTOS_CFLAGS="$(CFLAGS)" python3 tools/build_bearssl.py
build/trust_roots.h: trust/roots.pem tools/trust_der.py | build
	python3 tools/trust_der.py $< $@
build/tls.o: build/trust_roots.h build/bearssl.ready
build/tls_profile.o build/entropy.o build/tls_selftest.o build/x509_policy.o: build/bearssl.ready
build/kernel.elf: $(KERNEL_OBJS) kernel/link.ld build/libbearssl.a build/libmbedtls.a build/liblwip.a
	$(LD) -m elf_i386 --gc-sections -T kernel/link.ld $(KERNEL_OBJS) build/libbearssl.a build/libmbedtls.a build/liblwip.a -o $@
build/kernel.bin: build/kernel.elf
	objcopy -O binary $< $@
build/notos.img: build/boot.bin build/kernel.bin tools/image.py
	python3 tools/image.py
ifneq ($(wildcard build/tls-fixtures/valid.pem),)
ifeq ($(shell openssl x509 -checkend 3600 -noout -in build/tls-fixtures/valid.pem >/dev/null 2>&1 || echo expired),expired)
.PHONY: build/tls-fixtures.ready
endif
endif
build/tls-fixtures.ready: tools/tls_fixtures.py | build
	python3 tools/tls_fixtures.py
build/test_trust_roots.h: build/tls-fixtures.ready tools/trust_der.py
	python3 tools/trust_der.py build/tls-fixtures/ca.pem $@
build/tls_test.o: kernel/tls.c $(wildcard kernel/*.h) build/test_trust_roots.h build/bearssl.ready
	$(CC) $(CFLAGS) -Ikernel/freestanding -I$(BEARSSL_INC) $(MBEDTLS_FLAGS) -DNOTOS_TRUST_HEADER='"../build/test_trust_roots.h"' -c $< -o $@
build/kernel-tls-test.elf: $(filter-out build/tls.o,$(KERNEL_OBJS)) build/tls_test.o kernel/link.ld build/libbearssl.a build/libmbedtls.a build/liblwip.a
	$(LD) -m elf_i386 --gc-sections -T kernel/link.ld $(filter-out build/tls.o,$(KERNEL_OBJS)) build/tls_test.o build/libbearssl.a build/libmbedtls.a build/liblwip.a -o $@
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
test: build/fs-unit build/line-unit build/shell-parse-unit build/path-unit build/argument-unit build/float-unit build/poll-unit build/notos-socket-test.img all build/vfs-unit build/wait-unit build/elf-unit build/tls-port-unit build/integer-unit build/notos-tls-test.img build/certificate-unit build/utc-unit build/notlang-unit build/mouse-unit build/lang-unit build/net-unit build/clock-unit build/physical-unit build/heap-unit build/user-unit build/scheduler-unit
	./build/fs-unit
	./build/line-unit
	./build/shell-parse-unit
	./build/path-unit
	./build/argument-unit
	./build/float-unit
	./build/poll-unit
	python3 tests/lognames_unit.py
	./build/vfs-unit
	./build/wait-unit
	./build/elf-unit
	./build/tls-port-unit
	./build/integer-unit
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
	python3 tests/poll.py
	python3 tests/sockets.py
	python3 tests/vfs.py
	python3 tests/libc.py
	python3 tests/pipeline.py
	python3 tests/shell.py
	python3 tests/curl.py
	python3 tests/curl_https.py
	python3 tests/elf.py
	python3 tests/user.py
	python3 tests/multitask.py
	python3 tests/notlang.py
	python3 tests/mouse.py
	python3 tests/process.py
	python3 tests/wait.py
	python3 tests/storage.py
	python3 tests/files.py
	python3 tests/continuation.py
	python3 tests/tls_platform.py
	python3 tests/https.py
	python3 tests/network.py
test-online: all
	python3 tests/online.py
	python3 tests/https_online.py
	python3 tests/sockets_online.py
	python3 tests/poll_online.py
	python3 tests/curl_online.py
clean:
	rm -rf build

MBEDTLS_INC := build/mbedtls/mbedtls-3.6.5/include
MBEDTLS_FLAGS := -Ikernel -I$(MBEDTLS_INC) -DMBEDTLS_CONFIG_FILE='"notos_mbedtls_config.h"'
build/mbedtls.ready: tools/fetch_mbedtls.py | build
	python3 tools/fetch_mbedtls.py
build/libmbedtls.a: build/mbedtls.ready tools/build_mbedtls.py kernel/notos_mbedtls_config.h kernel/tls_platform_port.h $(wildcard kernel/freestanding/*.h)
	CC="$(CC)" NOTOS_CFLAGS="$(CFLAGS)" python3 tools/build_mbedtls.py

build/tls.o build/tls_test.o build/tls_platform_port.o: build/mbedtls.ready

build/tls_profile.o build/tls_selftest.o: build/mbedtls.ready
build/integer-unit: tests/integer_unit.c kernel/integer_runtime.c kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/integer_unit.c kernel/integer_runtime.c -o $@
build/tls-port-unit: tests/tls_port_unit.c kernel/tls_platform_port.c kernel/tls_platform_port.h build/mbedtls.ready | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(MBEDTLS_FLAGS) tests/tls_port_unit.c kernel/tls_platform_port.c -o $@

# Compact file alignment does not change the 4KiB-separated virtual segments.
build/elf-demo.o: user/elf_demo.c kernel/base.h kernel/syscall_numbers.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/elf-nx-heap.o: user/elf_demo.c kernel/base.h kernel/syscall_numbers.h | build
	$(CC) $(CFLAGS) -DDEMO_FAULT=1 -c $< -o $@
build/elf-nx-stack.o: user/elf_demo.c kernel/base.h kernel/syscall_numbers.h | build
	$(CC) $(CFLAGS) -DDEMO_FAULT=2 -c $< -o $@
build/elf-stack-guard.o: user/elf_demo.c kernel/base.h kernel/syscall_numbers.h | build
	$(CC) $(CFLAGS) -DDEMO_FAULT=3 -c $< -o $@
build/elf-readonly.o: user/elf_demo.c kernel/base.h kernel/syscall_numbers.h | build
	$(CC) $(CFLAGS) -DDEMO_FAULT=4 -c $< -o $@
build/elf-sleep.o: user/elf_demo.c kernel/base.h kernel/syscall_numbers.h | build
	$(CC) $(CFLAGS) -DDEMO_FAULT=5 -c $< -o $@
build/elf-%.elf: build/elf-%.o user/elf_link.ld
	$(LD) -m elf_i386 --gc-sections -z max-page-size=16 -T user/elf_link.ld $< -o $@
build/elf-%.bin: build/elf-%.elf
	cp $< $@
build/elf-%-blob.o: build/elf-%.bin
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.rodata.elfimage,alloc,load,readonly,data,contents --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly $< $@
build/elf-unit: tests/elf_unit.c kernel/elf.c kernel/elf.h kernel/base.h build/elf-demo.bin | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/elf_unit.c kernel/elf.c -o $@

build/wait-unit: tests/wait_unit.c kernel/wait_policy.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror tests/wait_unit.c -o $@

build/picolibc.ready: tools/fetch_picolibc.py | build
	python3 tools/fetch_picolibc.py
build/picolibc-built.ready: build/picolibc.ready tools/build_picolibc.py Makefile
	CC="$(CC)" NOTOS_CFLAGS="$(CFLAGS)" python3 tools/build_picolibc.py
LIBC_INCLUDES := -Iuser/include -nostdinc -isystem build/sysroot/include/notos -isystem $(shell $(CC) -print-file-name=include)
LIBC_FLAGS := $(CFLAGS) $(LIBC_INCLUDES) -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_POSIX_MONOTONIC_CLOCK=200809L -U__linux__ -U__unix__ -D__NOTOS__
build/libc-demo.o: user/libc/demo.c user/libc/os.h user/include/sys/_wait.h user/include/sys/dirent.h kernel/platform_abi.h kernel/file_abi.h kernel/process_abi.h kernel/syscall_numbers.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/libc-port.o: user/libc/port.c user/libc/os.h user/include/sys/_wait.h user/include/sys/dirent.h kernel/platform_abi.h kernel/file_abi.h kernel/process_abi.h kernel/syscall_numbers.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/libc-start.o: user/libc/start.c build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/libc-crt.o: user/libc/crt0.S | build
	$(CC) -m32 -c $< -o $@
build/user-integer-runtime.o: kernel/integer_runtime.c kernel/base.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/libc-demo.elf: build/libc-select.o build/libc-poll.o build/libc-mkdir.o build/libc-demo.o build/libc-port.o build/libc-start.o build/libc-crt.o build/user-integer-runtime.o user/elf_link.ld
	$(LD) -m elf_i386 --gc-sections -z noexecstack -z max-page-size=16 -T user/elf_link.ld --Map=build/libc-demo.map $(filter %.o,$^) build/sysroot/lib/notos/libc.a -o $@

build/libc-demo-blob.o: build/libc-demo.elf
	objcopy --strip-all $< build/libc-app.elf
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.rodata.libcimage,alloc,load,readonly,data,contents --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly build/libc-app.elf $@

build/vfs-unit: tests/vfs_unit.c kernel/vfs.c kernel/fd.c kernel/vfs.h kernel/fd.h kernel/file_abi.h kernel/process_abi.h kernel/syscall_numbers.h kernel/system_file.h kernel/fs.h kernel/platform_tls.h kernel/physical.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -no-pie tests/vfs_unit.c kernel/vfs.c kernel/fd.c -o $@

# lwIP's raw core and the separately linked Picolibc socket client.
LWIP_FLAGS := -Ikernel/lwip_port -Ibuild/lwip/lwip-STABLE-2_2_1_RELEASE/src/include
build/lwip.ready: tools/fetch_lwip.py | build
	python3 tools/fetch_lwip.py
build/liblwip.a: build/lwip.ready tools/build_lwip.py kernel/lwip_port/lwipopts.h kernel/lwip_port/arch/cc.h
	CC="$(CC)" NOTOS_CFLAGS="$(CFLAGS)" python3 tools/build_lwip.py
build/socket_stack.o: build/lwip.ready kernel/lwip_port/lwipopts.h kernel/lwip_port/arch/cc.h
build/libc-socket.o: user/libc/socket.c user/libc/os.h kernel/file_abi.h kernel/platform_abi.h kernel/socket_abi.h kernel/syscall_numbers.h $(wildcard user/include/*/*.h) user/include/netdb.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/socket-demo.o: user/socket_demo.c user/libc/os.h kernel/file_abi.h kernel/platform_abi.h kernel/syscall_numbers.h $(wildcard user/include/*/*.h) user/include/netdb.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/socket-demo.elf: build/poll-demo.o build/libc-poll.o build/socket-demo.o build/libc-socket.o build/libc-port.o build/libc-start.o build/libc-crt.o build/user-integer-runtime.o user/elf_link.ld
	$(LD) -m elf_i386 --gc-sections -z noexecstack -z max-page-size=16 -T user/elf_link.ld --Map=build/socket-demo.map $(filter %.o,$^) build/sysroot/lib/notos/libc.a -o $@
build/socket-demo-blob.o: build/socket-demo.elf
	objcopy --strip-all $< build/socket-app.elf
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.rodata.socketimage,alloc,load,readonly,data,contents --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly build/socket-app.elf $@
# Unprivileged fixture DNS; production always uses port 53.
build/liblwip-test.a: build/lwip.ready tools/build_lwip.py kernel/lwip_port/lwipopts.h kernel/lwip_port/arch/cc.h
	CC="$(CC)" NOTOS_CFLAGS="$(CFLAGS)" LWIP_OUTPUT=$@ LWIP_EXTRA_FLAGS=-DNOTOS_DNS_PORT=53530 python3 tools/build_lwip.py
build/kernel-socket-test.elf: $(KERNEL_OBJS) kernel/link.ld build/libbearssl.a build/libmbedtls.a build/liblwip-test.a
	$(LD) -m elf_i386 --gc-sections -T kernel/link.ld $(KERNEL_OBJS) build/libbearssl.a build/libmbedtls.a build/liblwip-test.a -o $@
build/kernel-socket-test.bin: build/kernel-socket-test.elf
	objcopy -O binary $< $@
build/notos-socket-test.img: build/boot.bin build/kernel-socket-test.bin tools/image.py
	python3 tools/image.py build/kernel-socket-test.bin $@

build/libc-poll.o: user/libc/poll.c user/libc/os.h user/include/poll.h kernel/poll_abi.h kernel/syscall_numbers.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/poll-unit: tests/poll_unit.c kernel/poll_wait.c $(wildcard kernel/*.h) | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -no-pie tests/poll_unit.c kernel/poll_wait.c -o $@
build/socket-demo.o: user/include/poll.h kernel/poll_abi.h

build/poll-demo.o: user/poll_demo.c user/poll_demo.h user/libc/os.h user/include/poll.h kernel/poll_abi.h kernel/syscall_numbers.h $(wildcard user/include/*/*.h) build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/socket-demo.o: user/poll_demo.h

# --- curl 8.11.1 HTTP/HTTPS port ---
build/curl.ready: tools/fetch_curl.py | build
	python3 tools/fetch_curl.py

build/libcurl.a build/libcurltool.a &: build/curl.ready tools/build_curl.py user/curl_port/curl_config.h build/picolibc-built.ready build/mbedtls.ready user/curl_port/notos_user_mbedtls_config.h kernel/notos_mbedtls_config.h kernel/tls_platform_port.h
	CC="$(CC)" python3 tools/build_curl.py

build/libc-select.o: user/libc/select.c user/libc/os.h kernel/file_abi.h kernel/platform_abi.h kernel/syscall_numbers.h user/include/poll.h kernel/poll_abi.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@

build/libc-mkdir.o: user/libc/mkdir.c user/libc/os.h kernel/syscall_numbers.h kernel/file_abi.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@

build/user-float-runtime.o: user/float_runtime.c kernel/base.h | build
	$(CC) $(CFLAGS) -c $< -o $@

CURL_LIBC_OBJS := build/libc-port.o build/libc-start.o build/libc-crt.o \
    build/libc-socket.o build/libc-poll.o build/libc-select.o \
    build/libc-mkdir.o build/user-integer-runtime.o build/user-float-runtime.o

build/curl.elf: build/libcurltool.a build/libcurl.a build/libuser-mbedtls.a build/user-tls-port.o build/user-certificate-policy.o $(CURL_LIBC_OBJS) user/elf_link.ld build/picolibc-built.ready
	$(LD) -m elf_i386 --gc-sections -z noexecstack -z max-page-size=16 \
	  -T user/elf_link.ld --Map=build/curl.map \
	  --wrap=mbedtls_ssl_config_defaults --wrap=mbedtls_ssl_conf_cert_profile --wrap=mbedtls_ssl_conf_verify --wrap=mbedtls_ssl_read \
	  --whole-archive build/libcurltool.a --no-whole-archive build/libcurl.a $(CURL_LIBC_OBJS) build/user-tls-port.o build/user-certificate-policy.o build/libuser-mbedtls.a \
	  build/sysroot/lib/notos/libc.a -o $@

build/curl-blob.o: build/curl.elf
	objcopy --strip-all $< build/curl-app.elf
	objcopy -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.rodata.curlimage,alloc,load,readonly,data,contents \
	  --add-section .note.GNU-stack=/dev/null \
	  --set-section-flags .note.GNU-stack=noload,readonly \
	  build/curl-app.elf $@


build/argument-unit: tests/argument_unit.c kernel/argument_core.c kernel/argument_core.h | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -no-pie tests/argument_unit.c kernel/argument_core.c -o $@

build/float-unit: tests/float_unit.c user/float_runtime.c | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -no-pie -DNOTOS_HOST_TEST -DFLOAT_TEST_PREFIX tests/float_unit.c user/float_runtime.c -o $@

build/path-unit: tests/path_unit.c kernel/path_core.c kernel/path_core.h kernel/file_abi.h kernel/base.h kernel/syscall_numbers.h | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/path_unit.c kernel/path_core.c -o $@

USER_TLS_FLAGS := -Iuser/curl_port -I$(MBEDTLS_INC) -DMBEDTLS_CONFIG_FILE='"notos_user_mbedtls_config.h"'
build/libuser-mbedtls.a: build/mbedtls.ready build/picolibc-built.ready tools/build_user_tls.py user/curl_port/notos_user_mbedtls_config.h kernel/notos_mbedtls_config.h kernel/tls_platform_port.h
	CC="$(CC)" NOTOS_CFLAGS="$(CFLAGS)" python3 tools/build_user_tls.py
build/user-tls-port.o: user/curl_port/tls_port.c user/curl_port/notos_user_mbedtls_config.h kernel/notos_mbedtls_config.h kernel/certificate_policy.h build/mbedtls.ready build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) $(USER_TLS_FLAGS) -c $< -o $@
build/user-certificate-policy.o: kernel/certificate_policy.c kernel/certificate_policy.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/system-ca.o: trust/roots.pem | build
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.rodata.systemca,alloc,load,readonly,data,contents --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly $< $@

build/line-unit: tests/line_unit.c kernel/line_core.c kernel/line_core.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -no-pie tests/line_unit.c kernel/line_core.c -o $@
build/shell-parse-unit: tests/shell_parse_unit.c kernel/shell_parse_core.c kernel/shell_parse_core.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -no-pie tests/shell_parse_unit.c kernel/shell_parse_core.c -o $@
build/fs-unit: tests/fs_unit.c kernel/fs.c kernel/fs_core.c kernel/fs_core.h kernel/fs.h kernel/disk.h kernel/platform_tls.h kernel/syscall_numbers.h kernel/file_abi.h kernel/base.h | build
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -no-pie tests/fs_unit.c kernel/fs.c kernel/fs_core.c -o $@

build/sh-main.o: user/sh/main.c user/libc/os.h user/include/sys/_wait.h user/include/sys/dirent.h kernel/line_core.h kernel/shell_parse_core.h kernel/platform_abi.h kernel/file_abi.h kernel/process_abi.h kernel/syscall_numbers.h build/picolibc-built.ready
	$(CC) $(LIBC_FLAGS) -c $< -o $@
build/user-line-core.o: kernel/line_core.c kernel/line_core.h kernel/base.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/user-shell-parse.o: kernel/shell_parse_core.c kernel/shell_parse_core.h kernel/base.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/sh.elf: build/sh-main.o build/user-line-core.o build/user-shell-parse.o build/libc-port.o build/libc-start.o build/libc-crt.o build/user-integer-runtime.o user/elf_link.ld
	$(LD) -m elf_i386 --gc-sections -z noexecstack -z max-page-size=16 -T user/elf_link.ld --Map=build/sh.map $(filter %.o,$^) build/sysroot/lib/notos/libc.a -o $@
build/sh-blob.o: build/sh.elf
	objcopy --strip-all $< build/sh-app.elf
	objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.rodata.shimage,alloc,load,readonly,data,contents --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly build/sh-app.elf $@

