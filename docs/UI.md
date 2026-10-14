# Mouse, VGA Graphical Controls, and Shell Line Editing

## PS/2 Mouse & VGA Desktop (`kernel/mouse.c`, `kernel/gui.c`)

NotOS drives a standard 3-byte PS/2 mouse on **IRQ12**:
- The i8042 controller is initialized with the PIC masked, enabling the second
  PS/2 port and verifying `ACK` (`0xFA`) responses for `0xF6` (set defaults) and
  `0xF4` (enable data reporting).
- `IRQ1` and `IRQ12` share the i8042 status/data drain, routing `AUX` bytes into
  the mouse queue and keyboard scancodes into the keyboard queue without
  performing rendering inside the IRQ handler.
- The pure packet decoder (`kernel/mouse_decode.c`) extracts sign bits and
  button states, discards overflow packets, and resynchronizes on bit 3 of the
  first packet byte.

During each event-loop pass, the kernel processes up to 128 queued mouse bytes
and renders an **8×8 cursor** clipped to `320×200` (saving and restoring the
underlying VGA framebuffer pixels so console output never leaves cursor trails):
- **`CLR`**: Clears the terminal and discards any partial input line.
- **`FILES`**: Lists the shell's current working directory.
- **`TASKS`**: Displays active background processes.
- **Title bar click**: Minimizes or restores the terminal window (while
  minimized, the console buffer continues recording output).
- `mouse`: Prints current cursor coordinates, button states, decoded packet
  count, and dropped bytes.

## Interactive Shell Line Editing (`kernel/line_core.c`, `kernel/line_input.c`)

The shell provides interactive line editing and history across both the **PS/2
keyboard** and **`COM1` serial CSI/SS3 escape sequences** outside IRQ context:
- **Command History**: 8-entry circular history navigated with `Up` and `Down`,
  preserving the in-progress draft line and skipping consecutive duplicate
  commands.
- **Cursor Editing**: `Left`, `Right`, `Home`, `End`, `Backspace`, and `Delete`
  edit the current line in place without stealing input queued for background
  processes (`feed`).
- **`Tab` Completion**: Completes shell command names (on the first token) or
  VFS file/directory path components (against the shell's `cwd`) by longest
  common prefix, appending `/` when completing a unique directory and listing
  candidates when ambiguous.
- **Line Length Limits**: Up to **159 characters** for standard shell commands
  and up to **320 characters** for `curl` and `spawn-elf` (whose arguments
  remain bounded to 256 bytes).

## Verification

- `tests/mouse_unit.c` and `tests/line_unit.c` (ASAN/UBSAN): Test PS/2 3-byte
  packet resynchronization/overflow and pure line editing, cursor movement,
  draft preservation, and history rotation.
- `tests/mouse.py` and `tests/continuation.py`: Verify QMP mouse movement/clicks,
  toolbar actions, window minimize/restore, and PS/2 + serial CSI arrow/Home/End/
  Delete/`Tab` completion in QEMU.
