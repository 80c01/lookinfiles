# Plan: one SSH connection per machine on Windows (libssh)

Status: **in progress**. Step 1 is done; of step 2, `ssh_exec_sync()` and
`ssh_exec_async()` run on exec channels (`core/ssh_link_exec`), while
`ssh_spawn()` (Disk Usage, deep search, Git mirror) still starts `ssh.exe`.

## The problem

On Linux every remote command goes through one OpenSSH master connection
per machine (`ControlMaster`, `core/ssh_session.c`): the password is asked
once. The Windows OpenSSH client (`ssh.exe`) has no multiplexing, so every
SFTP session and every remote command is a connection of its own, and each
one may ask for the password again:

| Who                                         | How it reaches the machine                 |
| ------------------------------------------- | ------------------------------------------ |
| files (`core/vfs`)                          | `sftp_client_spawn()`: `ssh -s host sftp`  |
| System Monitor (every second), Diagnostics  | `ssh_exec_async()`                         |
| Git badges / branch (`git_remote`), Changes | `ssh_exec_async()`, `vfs_exec()`           |
| search, Go to File, Deep Search, Disk Usage | `vfs_exec()`, `vfs_spawn()`                |
| Git tab copy (`git_mirror`)                 | `vfs_spawn()`                              |
| language servers (`code_intel`)             | `vfs_command_argv()` (a local process)     |
| Jupyter kernels (`ipynb_run`)               | `ssh_session_argv()` (a local process)     |
| terminal (`terminal.c`)                     | `ssh_session_argv(SSH_ARGV_TTY \| NO_MUX)` |

Besides the prompts, a full SSH handshake each second (System Monitor) is
slow, fills the server's logs and may get the address banned (fail2ban).

Today's workaround: a key with ssh-agent (no prompt at all). This plan is
the real fix: the application owns the connection and opens SSH channels
on it, as the OpenSSH master does.

## Decision to revisit

The remote work chose "OpenSSH `ssh` binary + in-house SFTP, no libssh".
This plan adds libssh (LGPL, in MSYS2 as `mingw-w64-x86_64-libssh`, in
Debian/Ubuntu as `libssh-dev`) as an **optional** dependency
(`HAVE_LIBSSH`, like VTE or poppler). Without it, nothing changes.

Rejected alternative: a single `ssh.exe` carrying our own multiplexing
protocol. It needs a demultiplexing program on the machine (Python, Perl,
or an uploaded binary per architecture): fragile, and it rebuilds what SSH
channels already do.

## Design

One `SshLink` per machine: a libssh session, authenticated once, living on
a thread of its own with its own `GMainContext` (as `core/vfs` does). Every
user of the machine opens **channels** on it. libssh is non-blocking on
that thread (`ssh_set_blocking(session, 0)` + a poll source on its socket):
a slow reader never blocks the others nor the UI thread.

Lesson of the terminal freeze (Linux, 2026-11): remote terminals went
through the OpenSSH master; when one terminal stopped reading, the master
wrote to it with blocking writes and every other user of the machine froze
with it, the application included (ssh had to be killed with `kill -9`).
Since then terminals use `SSH_ARGV_NO_MUX`. Pipes (exec, sftp) are fine.
The link must never repeat this: one channel's full buffer stops reading
*that* channel only, and no write of the link thread ever blocks.

### New modules (layer 1, `core/`, GIO + libssh, no widgets, unit-tested)

- **`core/ssh_link`**: connect, authenticate, keep alive, close.
  - Options from `~/.ssh/config` (`ssh_options_parse_config()`), then the
    machine's own settings (host, user, port, identity file) over them.
  - Host key: checked against `~/.ssh/known_hosts`
    (`ssh_session_is_known_server()`); an unknown key is shown with its
    SHA-256 fingerprint through the existing confirmation prompt (same
    behaviour as `StrictHostKeyChecking=accept-new` today: a new host is
    added after a "yes", a **changed** key is refused with ssh's message).
  - Authentication order: agent, public keys (passphrase asked through the
    prompt), keyboard-interactive (each question through the prompt), then
    password. The prompts reuse the `SshAskpassFunc` dialogs of
    `remote_session.c` (`on_ask()`), called on the main thread through a
    small async request; the link thread waits for the answer, never the
    UI thread. Secrets are wiped after use and never kept: once
    authenticated, the session needs none.
  - Keep-alive every 15 s, 3 missed = lost (as `ServerAliveInterval=15`,
    `ServerAliveCountMax=3`). On loss: every channel fails with
    `G_IO_ERROR_CONNECTION_CLOSED`; reconnecting asks again (once, not in
    a loop: same rule as `ssh_session_on_master_missing()`).
- **`core/ssh_channel`**: a channel as a `GIOStream` (input = stdout,
  output = stdin), plus its stderr as a `GInputStream` and its exit status.
  Kinds: `exec` (a command line), `subsystem` (`sftp`), `shell` with a pty
  (size, resize). Reads and writes are queued to the link thread; the
  stream methods are the async ones (sync versions only for worker
  threads, as `vfs` calls).

### Wiring (step by step, each a commit that builds and passes)

1. **Link + SFTP.** `sftp_client_spawn()` gets a libssh path: when the
   machine has an `SshLink`, open a `subsystem sftp` channel and call
   `sftp_client_new(in, out)` on its streams (the client already takes any
   pair of streams; nothing else in `vfs` changes). `remote_session_connect()`
   opens the link instead of `ssh_master_start()` when libssh is in use.
   Result: browsing, previews, save: one password.
2. **Remote commands.** `ssh_exec_sync()` / `ssh_exec_async()` /
   `ssh_spawn()` open an `exec` channel. `ssh_spawn()` returns a
   `GSubprocess` today: change it (and `vfs_spawn()`) to return a small
   `SshProcess` interface (stdout stream, stdin stream, wait/exit status,
   force-exit) implemented both by a `GSubprocess` and by a channel; the
   few callers (`git_mirror`, `deep_search`, `disk_scan`) adapt.
   Result: System Monitor, Git badges, search, Disk Usage: no more prompts,
   and a channel opens in milliseconds instead of a handshake.
3. **Programs that want a command line** (kernels, language servers):
   they start a local process from `ssh_session_argv()` /
   `vfs_command_argv()`. Give them a relay instead of `ssh.exe`:
   `lookinfiles --ssh-channel <port> <id>`, which copies its
   stdin/stdout/stderr to an `exec` channel the instance opened and exits
   with the channel's status. The callers do not change:
   `ssh_session_argv()` returns the relay's argv when the link is in use.
   Protocol: see "Relay protocol" below.
4. **Terminal.** The same relay with a pty channel (the ticket says so).
   Optional: a terminal may also keep using `ssh.exe` (one prompt per
   terminal, acceptable).
5. **Switch.** On Windows, the link is used when built with libssh. On
   Linux, keep the OpenSSH master by default; a setting (or environment
   variable for testing) may select libssh there too, so both platforms
   can run the same code later. `ssh_session_can_multiplex()` becomes
   "is there a shared connection" (master or link), so the Windows
   special cases added in 04e79cf (`connect_files()`, the `no_login` guard
   in `vfs.c`) become the fallback for builds without libssh.
6. **Diagnostics and docs.** The Diagnostics tab says which transport a
   machine uses; `docs/packaging.md` lists the libssh DLLs (libssh,
   its crypto library, zlib) for the Windows bundle; `make deps` lists
   libssh as optional; user guide `remote.md` updated (and its l10n page).

### Implementation decisions

Settled here so that every step is written the same way.

- **Modules and layers.** `SshTarget` moves to a header-only
  `core/ssh_target.h` (`ssh_session.h` includes it: callers do not change).
  Rules in `tools/check_layers.sh`: `ssh_channel:i18n`,
  `ssh_link:ssh_target,ssh_channel,i18n`, and `ssh_session` gains
  `ssh_target,ssh_link,ssh_channel`. `ssh_link` never includes
  `ssh_session` nor `ssh_askpass` (both would make a cycle): the dispatch
  "link or `ssh` program" lives in `ssh_session` and `sftp_client`.
- **Finding a machine's link.** A registry inside `ssh_link`:
  `ssh_link_lookup(const SshTarget*)` returns a new ref or NULL, keyed by
  `user@host:port` (empty user and port 0/22 normalised), guarded by a
  `GMutex` (the `vfs` thread looks it up). `remote_session` creates the link
  on connect, holds the owning ref and closes it on disconnect; a closed
  link leaves the registry at once.
- **Prompts.** `ssh_link` has its own callback,
  `SshLinkAskFunc(SshLinkAsk* ask, SshLinkAskKind kind, const gchar* prompt,
  gpointer data)`, answered once with `ssh_link_reply(ask, answer)`
  (secret, yes/no, notice: the same three kinds as `SshAskpassKind`, not
  the same type). It is always called on the main thread
  (`g_main_context_invoke()` on the default context). In `remote_session.c`
  the dialog body of `on_ask()` becomes a static helper taking a reply
  callback, shared by `on_ask()` and the new `on_link_ask()`.
- **`SshProcess`.** The three `ssh_spawn()` / `vfs_spawn()` callers
  (`git_mirror`, `deep_search`, `disk_scan`) use only stdin pipe, stdout
  pipe, `force_exit` and `wait`. `SshProcess` is a plain opaque struct in
  `ssh_session` with exactly `ssh_process_get_stdin()`,
  `ssh_process_get_stdout()`, `ssh_process_force_exit()`,
  `ssh_process_wait()` (returns the exit status, cancellable) and
  `ssh_process_free()`, wrapping either a `GSubprocess` or a channel. No
  GObject, no GInterface.
- **Threads.** Every libssh call runs on the link thread, none elsewhere
  (a libssh session is not thread-safe). Channel streams are `GIOStream`
  subclasses whose async methods post the request to the link thread and
  complete in the caller's thread-default context (the `vfs` thread for
  SFTP); the sync methods wait on a `GCond` and are refused on the main
  thread (`g_return_val_if_fail`).
- **Socket polling.** `core/ssh_watch` (`ssh_watch_new()`), a
  level-triggered source that never closes the socket. Not
  `g_socket_create_source()`: on Windows a `GSocket` remembers an event
  until its own I/O clears it, and libssh reads the socket itself, so the
  source stayed ready for ever (one core at 100 %, the connection never
  progressing; seen on the user's machine). Unix: a plain `GPollFD`.
  Windows: a thread waits in `WSAPoll()` and wakes the loop; never
  `WSAEventSelect()`, which forces the socket non-blocking: libssh switches
  it back to blocking once the TCP connection is made, that fails, and the
  session stays "connecting" until the timeout (seen on the user's machine).
  Ask for "writable" only when libssh has bytes to send or the TCP
  connection is still being made: a connected socket is always writable.
- **Keep-alive.** A 15 s timeout on the link context calls
  `ssh_send_keepalive()`; a missing reply counts as a miss, 3 misses = lost
  (libssh has no keep-alive of its own).
- **Build.** The Makefile detects `pkg-config --exists libssh` like
  `HAVE_POPPLER` and defines `HAVE_LIBSSH`. Without it `ssh_link.c` and
  `ssh_channel.c` are not built, the dispatch code is under `#ifdef`, and
  `test_ssh_link` / `test_ssh_channel` are not in `TESTS` (the test server
  needs `libssh/server.h`, part of `libssh-dev` and the MSYS2 package).

### Relay protocol (steps 3 and 4)

- **Module.** `core/ssh_relay` (layer 1), both sides: the server in the
  instance and the helper. Rule: `ssh_relay:ssh_target,ssh_link,ssh_channel,app_paths,i18n`;
  `ssh_session` includes `ssh_relay` (for the argv), never the opposite.
  `main.c` checks `ssh_relay_is_helper(argc, argv)` before any GTK call and
  returns `ssh_relay_helper_main(argc, argv)`, as it does for askpass. The
  program path is the one given to `ssh_askpass_serve()`.
- **Ticket in a private file, never on the command line.** Language
  servers (`lsp_client.c`) and kernels (`ipynb_kernel.c`) build their own
  `GSubprocessLauncher`, and the terminal spawns through VTE/ConPTY: a
  variable set with `ssh_session_set_env()` would not reach the relay. And
  a command line is readable by other local users (`/proc/*/cmdline` on
  Linux): no secret may go there. So every call of `ssh_session_argv()`
  that goes through a link registers a **ticket** in the instance:
  - 128 random bits (hex), bound to the link, the command line and the
    tty flag; **single use**; dropped after 60 s if unused.
  - Written to `app_runtime_dir()/relay/<id>` (the directory `0700`, the
    file created `0600` with `O_CREAT | O_EXCL`; on Windows the directory
    is under the user's profile). `<id>` is a counter, not a secret.
  - The argv is `<program> --ssh-channel <port> <id>`. The relay reads the
    file, **deletes it at once**, then sends the ticket. The instance also
    deletes the file when the ticket is used or expires, and every
    remaining one at exit.
  - No private runtime directory (`app_runtime_dir()` returns NULL): no
    relay, the machine uses the `ssh` program.
  The relay never chooses the command, so a ticket cannot run anything
  else; another user sees only the id, which opens nothing. Callers that
  run the same argv twice must call `ssh_session_argv()` again (check
  `ipynb_run` and `code_intel` rebuild it at each start).
- **Server.** `ssh_relay_serve()` listens on `127.0.0.1`, port 0 (chosen
  by the system), started with the first link, stopped with the last. It
  runs on the link thread side: accepting, reading and writing are async,
  and no write ever blocks.
- **Handshake.** The relay connects and sends three text lines:
  `LOOKIN-RELAY 1`, the ticket, then `<rows> <cols>` (the size of its
  terminal, `0 0` without a tty). The instance compares the ticket in
  constant time (as `same_secret()`), consumes it, opens the channel
  (`exec`, or `pty` of that size then `exec`/`shell`), and answers one line:
  `OK` or `ERR <message>` (then closes). Any other first line, a bad,
  used or expired ticket: `ERR` and close.
- **Frames** after `OK`, both ways: 1 byte type, 4 bytes length (big
  endian), payload of at most 32 KiB.
  - relay to instance: `I` stdin data; `E` stdin closed (empty); `W`
    window size (4 bytes: rows, cols, two big-endian `uint16`).
  - instance to relay: `O` stdout data; `R` stderr data; `X` exit status
    (4 bytes, big-endian signed; -1 when killed or unknown), then the
    instance closes the socket.
  - socket closed without `X` (link lost, instance gone): the relay prints
    `lookinfiles relay: connection lost` on stderr and exits 255, as ssh.
- **Flow control (instance side).** Stop reading the channel while more
  than 256 KiB wait for the relay's socket; stop reading the socket while
  the channel does not accept more (libssh's partial write in
  non-blocking mode). Each direction waits on its own, the link thread
  never does.
- **Relay side.** A separate process with nothing else to do: blocking I/O
  is fine there (one thread stdin to socket, the main one socket to
  stdout/stderr). With a tty: raw mode (Linux `cfmakeraw()`, restored at
  exit; Windows console mode with `ENABLE_VIRTUAL_TERMINAL_INPUT`, without
  line input, echo and processed input). Without a tty on Windows: stdin and
  stdout in binary mode (`_setmode(_O_BINARY)`). Window size: Linux
  `SIGWINCH` + `TIOCGWINSZ`; Windows has no signal, so poll
  `GetConsoleScreenBufferInfo()` every 250 ms (ConPTY updates it on
  resize) and send `W` when it changes.
- **Never a secret** goes through the relay: the link is authenticated
  before the ticket is issued. The relay writes no log.
- **Tests** (`tests/test_ssh_relay.c`, against the libssh test server):
  frames round-trip, exit status passed on, stderr kept apart, ticket used
  twice / expired / wrong refused, ticket file `0600` and gone after use, a relay that stops reading does not
  stall another channel of the same link, link lost gives exit 255.

### Tests (`tests/test_ssh_link.c`, `tests/test_ssh_channel.c`)

The sandbox has no sshd. libssh ships a server API (`libssh/server.h`): a
test server on a loopback port with a generated host key, password and
public-key auth, `exec` that runs a fixed set of commands, the `sftp`
subsystem bridged to the in-test POSIX SFTP server already used by
`test_sftp_client`. Cover: password once for many channels, wrong
password, unknown then known host key, changed host key refused, channel
streams under load (a slow reader does not stall another channel), lost
connection fails every channel, ASan clean.

## Risks and open questions

- **ssh_config coverage**: libssh reads Host, HostName, User, Port,
  IdentityFile, ProxyJump, but not every option. Exotic setups
  (complex ProxyCommand, GSSAPI, certificates, FIDO keys) may differ from
  `ssh.exe`. Fallback: a per-machine "use the ssh program" switch.
- **Windows ssh-agent**: whether libssh talks to the Windows OpenSSH agent
  (named pipe `\\.\pipe\openssh-ssh-agent`) must be checked first; if not,
  key-with-agent users would get a passphrase prompt per connection
  (still once per link, not per command).
- **Security**: host-key checking and secret handling move into the
  application; review them as such (no secret in logs, wiped buffers,
  relay token compared in constant time as `same_secret()`).
- **Size**: about as big as the sshfs removal; ~1 500 lines plus tests.
  Steps 1–2 alone remove nearly every prompt and are worth shipping first.
- **Testing**: only possible against a real server on the user's Windows
  machine; each step needs their test before the next.
