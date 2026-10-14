# Background Processes and Blocking Waits

## Process Table & Lifecycle (`kernel/process.c`)

```text
spawn worker
spawn spin
spawn fault
spawn-elf /system/libc.elf test
ps
kill PID
process-trace on
process-trace off
```

`process.*` manages a **four-slot background process table** with monotonic
32-bit PIDs that are never reused (if the 32-bit PID counter saturates, new
spawns are rejected). Process state lives in RAM across shell commands.

During each main event-loop iteration, `process_poll` wakes any processes whose
sleep, `stdin`, socket, or `poll` wait has completed, and `scheduler_pulse`
executes one `TASK_READY` process until its 5-tick CPL3 quantum expires, it
yields (`SYS_YIELD`), it blocks (`TASK_BLOCKED`), or it exits/faults—returning
to the kernel frame so networking, serial, keyboard, and mouse events are
serviced between pulses.

`kill PID` operates from the kernel event loop and never tears down a currently
executing context inside an IRQ handler. Normal exits, user faults, and budget
expirations (`124`) are automatically reaped outside IRQ context, freeing all
private user frames, page tables, open file descriptors, DNS registrations, and
the 16 KiB kernel stack.

## Blocking Process Waits (`TASK_BLOCKED`)

Managed background processes can block without busy-waiting or freezing the
shell:

1. **Timed Sleep (`SYS_SLEEP = 9`, `TASK_WAIT_NONE`)**:
   - `EBX` = `0..60000` ms (`0` yields; values `> 60000` return `-EINVAL`).
   - Saves the user trap frame, marks the task `TASK_BLOCKED`, and returns to
     the kernel event loop. Blocked ticks do not count against the task's CPL3
     CPU budget.
   - Relative deadline comparisons (`kernel/wait_policy.h`) use unsigned
     subtraction and handle 32-bit `clock_ticks` wraparound.
   - Synchronous Phase 5 diagnostic batches (`multitask-run`) have no event loop
     to wake sleeping tasks and reject positive sleep durations with
     `-EOPNOTSUPP`.
2. **Console / `stdin` Input (`TASK_WAIT_INPUT`)**:
   - Blocking `read` on `stdin` or `/dev/console` suspends the process until
     bytes or EOF arrive in its private 256-byte input queue via `feed PID TEXT`
     or `feed-eof PID`.
   - `process_poll` copies queued bytes into the blocked process's user buffer
     via supervisor physical frame aliases without switching `CR3`.
3. **TCP Socket Operations (`TASK_WAIT_CONNECT`, `TASK_WAIT_RECEIVE`,
   `TASK_WAIT_SEND`)**:
   - Blocking `connect`, `recv`/`read`, and `send`/`write` store the descriptor,
     validated user buffer, length/flags, and optional timeout (`SO_RCVTIMEO` /
     `SO_SNDTIMEO`, or 10 s default for `connect`). See [SOCKETS.md](SOCKETS.md).
4. **Multiplexed I/O (`TASK_WAIT_POLL`)**:
   - `poll` and `select` store a bounded snapshot of up to 64 `(fd, events)`
     pairs and a relative or infinite deadline. Ready events take priority if a
     deadline expires on the same iteration. See [POLL.md](POLL.md).
5. **Pipe Output Backpressure (`TASK_WAIT_OUTPUT`)**:
   - Blocking `write` on a full kernel pipe ring buffer (`4096` bytes) suspends
     the writer until a reader consumes bytes or all read ends close (`-EPIPE`).
6. **Child Termination Wait (`TASK_WAIT_CHILD`)**:
   - `wait` and `waitpid` (`SYS_WAIT = 44`) suspend the calling parent until a
     matching child exits/faults/is killed, or return immediately with `0` when
     `WNOHANG` (`NOTOS_WNOHANG = 1`) is set and a matching child is still running.

When resuming a managed process, the scheduler sanitizes `EFLAGS` (clearing
`NT`, `IOPL`, and `VM` while enforcing `IF=1`).

## Process Composition (`SYS_SPAWN`, `SYS_WAIT`, `SYS_GETPPID`, `SYS_KILL`, `SYS_PROCESS_LIST`) and `SYS_EXEC`

- `process_spawn_elf_args` passes a program name and a single unsplit argument;
  `process_spawn_elf_split` splits space-delimited argument text (up to 256
  bytes and 16 `argv` entries including `argv[0]`). Both construct a 16-byte
  aligned user stack with `argc`, `argv`, empty `envp`, and `AT_NULL`, rolling
  back all resources on error before incrementing the PID counter.
- `spawn-elf PATH [ARGS]` loads an ELF binary from `/system`, `/tmp`, or `/disk`.
- **`SYS_SPAWN` (`43`)**: Spawns a new child process without `fork` (`EBX=path`,
  `ECX=argv`, `EDX=envp`, `ESI=fd_map`, `EDI=map_count | (flags << 16)`).
  Accepts up to 16 `argv` strings, 16 `envp` strings, and 512 total string bytes
  (`kernel/process_abi.h`), plus up to 16 `struct notos_fd_map { int child_fd;
  int parent_fd; }` operations applied in order after inheriting non-`FD_CLOEXEC`
  parent descriptors (`NOTOS_FD_MAP_CLOSE = -1` closes `child_fd`). Inherits the
  parent's `cwd`, records `parent_id`, sets the slot name from the `argv[0]`
  basename (so multi-call applets in `/system/box.elf` appear under their applet
  name), and returns `-EAGAIN` when all 4 process slots are occupied (including
  unreaped zombies).
- **`SYS_WAIT` (`44`) & Zombie/Orphan Lifecycle**: When a process with a live
  parent exits, faults, or is killed, its address space, frames, FDs, pipes, and
  sockets are freed immediately while its slot retains `(zombie_id,
  zombie_status)` until the parent reaps it via `SYS_WAIT(pid_or_any, &status,
  flags)`. If a parent exits first, its live children are orphaned
  (`getppid() == 0` via `SYS_GETPPID = 45`) and any unreaped zombie children are
  freed immediately so slots never leak.
- **`SYS_KILL` (`47`)**: Terminates target process `pid` with signal `1..31`
  (exit status `128 + sig`, reported as `WIFSIGNALED(status)` /
  `WTERMSIG(status) == sig`), waking or reaping tasks blocked on pipes, sleep,
  `poll`, sockets, or children. Signal `0` validates target existence without
  terminating.
- **`SYS_PROCESS_LIST` (`49`)**: Read-only snapshot (`EBX=entries`,
  `ECX=max_entries`, up to 16 entries) of active and zombie process slots via
  44-byte `struct notos_process_info` records (`pid`, `ppid`, `state`,
  `wait_reason`, `cpu_ticks`, `exit_status`, `name[24]`), used by user-space
  `ps` (`/system/box.elf`).
- `notos_exec` (`SYS_EXEC = 37`) replaces the calling process's ELF image and
  address space in place while preserving its PID, `parent_id`, `cwd`, and open
  file descriptors (closing only `FD_CLOEXEC` slots) and canceling any pending
  async DNS tokens owned by the previous image.
- Standard tasks have a 250-tick CPL3 budget; `/system/curl.elf` receives a
  2,500-tick budget for TLS handshakes and X.509 parsing. `SYS_EXEC` does not
  reset consumed ticks.
- By default, `curl` and `SYS_SPAWN` children suppress kernel `PROCESS:` exit
  banners so pipeline output stays clean; `process-trace on` and
  `process-trace off` toggle those banners globally while user faults and
  cleanup errors are always reported.

