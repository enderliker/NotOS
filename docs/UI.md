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

## User-Space Shell (`/system/sh.elf` & `kernel/shell_parse_core.*`)

In addition to the built-in kernel diagnostic shell (which remains the default
boot shell unless built with `USER_SHELL_DEFAULT=1`), NotOS includes a real
ring 3 interactive shell at `/system/sh.elf`, launched in the foreground via the
kernel shell `sh` command:
- **Raw/No-Echo TTY Mode (`SYS_FCNTL`)**: `NOTOS_F_GETTTY` (`5`) and
  `NOTOS_F_SETTTY` (`6`) query and configure per-process `stdin` terminal flags
  (`NOTOS_TTY_CANON` and `NOTOS_TTY_ECHO`, default `NOTOS_TTY_DEFAULT = 3`).
  While `/system/sh.elf` runs in the foreground, the kernel routes PS/2 and
  `COM1` serial characters directly into the foreground process's `stdin` queue,
  and `sh.elf` compiles `line_core.*` directly into user space for 8-entry
  history, cursor editing, and command/VFS `Tab` completion.
- **Pure Shell Parser (`kernel/shell_parse_core.*`)**: Parses command lines up to
  512 bytes into up to 8 sequential/background jobs (each with up to 4 pipeline
  stages and 16 `argv` tokens) without dynamic allocation, supporting single
  quotes (`'...'`), double quotes (`"..."`), backslash escapes (`\`), `$VAR`,
  `${VAR}`, and `$?` expansion (re-evaluated before each top-level `;` or `&`
  job), pipelines (`|`), input/output redirections (`<`, `>`, `>>`), background
  execution (`&`), and sequential lists (`;`).
- **Builtins & Execution**: Supports `cd`, `exit`, `export`, `unset`, `jobs`,
  and `wait`. Background jobs (`&`) receive `stdin` closed (`NOTOS_SPAWN_FD_CLOSE`)
  unless explicitly redirected via `<`. Multi-stage pipelines connect children
  with `SYS_PIPE` (`46`) and `SYS_SPAWN` (`43`) and propagate the last stage's
  exit status (`$?`), recovering cleanly when the 4-slot process table is
  exhausted.

## Verification

- `tests/mouse_unit.c`, `tests/line_unit.c`, and `tests/shell_parse_unit.c`
  (ASAN/UBSAN): Test PS/2 3-byte packet resynchronization/overflow, pure line
  editing/history rotation, and shell quoting, expansion, pipelines,
  redirections, and syntax error rejection.
- `tests/mouse.py`, `tests/continuation.py`, and `tests/shell.py`: Verify QMP
  mouse movement/clicks, toolbar actions, window minimize/restore, PS/2 + serial
  CSI arrow/Home/End/Delete/`Tab` completion, and `/system/sh.elf` interactive
  editing, builtins, pipelines, redirections to `/tmp` and `/disk`, background
  jobs, and 4-slot exhaustion in QEMU.
