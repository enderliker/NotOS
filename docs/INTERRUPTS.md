# IDT, IRQs, PIC/PIT, and Exceptions

## Interrupt Architecture

- **Custom 256-entry IDT**: 32-bit interrupt gates (`DPL=0`, kernel code
  selector `0x08`), plus `INT 0x80` (`DPL=3`) for user-space system calls.
- **Uniform x86 trap frame (`kernel/isr.S`)**: Normalizes dummy error codes for
  vectors that do not push one, saves general-purpose and segment registers,
  clears `EFLAGS.DF` (`cld`), and aligns the stack before calling C. `IRET`
  restores `EIP`, `CS`, `EFLAGS`, and (on CPL3 transitions) user `ESP`/`SS`.
  Trap frame field offsets are verified at compile time with `_Static_assert`.
- **8259 PIC remapping (`kernel/pic.c`)**: Maps master `IRQ0..7` to vectors
  `32..39` and slave `IRQ8..15` to vectors `40..47`. Unmasks `IRQ0` (PIT),
  `IRQ1` (PS/2 keyboard), `IRQ2` (slave cascade), and `IRQ12` (PS/2 mouse when
  present); all other IRQ lines remain masked.
- **Spurious IRQ handling**: Checks the master/slave PIC In-Service Register
  (ISR) on `IRQ7` and `IRQ15` before sending EOI (sending master EOI only when
  a spurious slave `IRQ15` occurs).
- **PIT Channel 0 (`kernel/clock.c`)**: Configured in Mode 2 (rate generator)
  with divisor `1193` (~1000.15 Hz), using exact fractional remainder
  accumulation against the nominal `1,193,182 Hz` crystal frequency so
  millisecond uptime does not drift.

## CPU Exceptions & Faults

`INT3` (breakpoint, vector 3) is recoverable in ring 0 and used for self-tests.
Other exceptions originating in ring 0 trigger a fatal kernel panic screen
displaying the vector, mnemonic, error code, `EIP`, `CS`, `EFLAGS`, general
registers, and `CR2` (on `#PF`), and halt the CPU with `cli; hlt`.

Exceptions originating in **ring 3 (CPL 3)** terminate only the faulting user
task/process, record the fault vector/error/`CR2`, release the task's resources
outside IRQ context, and return cleanly to the shell or next ready task.

```text
int3
fault divide
fault opcode
fault gp
fault null
fault readonly
fault guard
fault unmapped
fault heap-end
```

**Note**: The `fault <kind>` shell commands intentionally trigger ring 0 CPU
faults to test the kernel panic handler and require restarting QEMU afterward.
`int3` is non-destructive and verifies register, `CF`, `DF`, and `IF`
preservation across `IRET`.

## Clock & Timing Commands

```text
uptime
sleep 200
busy 250
irqs
irq-test
```

- `uptime`: Prints accumulated milliseconds, `IRQ0` tick count, and `IF` state.
- `sleep MS`: Idles the CPU using `HLT`. Checks the deadline with interrupts
  disabled and executes `sti; hlt` atomically to prevent lost-wakeup races.
- `busy MS`: Spins in the foreground for `0..5000` ms while `IRQ0` updates the
  clock and `IRQ1`/`IRQ12` continue queueing PS/2 input.
- `irqs`: Displays counters for timer ticks, keyboard/mouse interrupts,
  unexpected IRQs, dropped scancodes, and spurious `IRQ7`/`IRQ15` events.
- `irq-test`: Dispatches software interrupts to vectors `39` (`IRQ7`) and `47`
  (`IRQ15`) with empty PIC ISR bits to verify spurious IRQ handling.

IRQ handlers never draw UI, interpret commands, or allocate/free memory. They
update tick counters, enqueue raw PS/2 bytes, send PIC EOI, and (on `IRQ0`)
select the next ready user trap frame when a quantum or budget expires.

## PS/2 Keyboard & Controller (`kernel/keyboard.c`, `kernel/mouse.c`)

The i8042 controller is initialized with interrupts disabled and the PIC masked.
Port 1 (`IRQ1`) is configured with Scancode Set 1 translation, and Port 2
(`IRQ12`) is enabled for the 3-byte PS/2 mouse. The shared controller drain
routes `AUX` bytes to the mouse ring buffer and keyboard scancodes to a
255-entry circular queue, discarding controller `ACK` (`0xFA`) and parity/timeout
errors. Dequeueing uses a brief critical section that preserves `EFLAGS.IF`.
Scancode translation, modifier tracking, and shell line editing run outside IRQ
context in the main loop.

## Scope & Limits

- Tested in QEMU TCG emulation; physical hardware, APIC, HPET, and SMP are not
  implemented.
- There is no separate TSS or alternate supervisor stack for double faults
  (`#DF`).
- Keeping interrupts disabled for extended periods can cause the 8259 PIC to
  coalesce timer ticks; the clock is not a hard real-time source.
- The `COM1` serial port and RTL8139 NIC remain polled from the main event loop.
