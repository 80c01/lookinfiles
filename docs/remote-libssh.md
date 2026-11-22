# Plan: one SSH connection per machine on Windows (libssh)

Status: **not started**. A design kept for a future implementation.

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
a slow reader never blocks the others nor the UI thread (lesson of the
terminal freeze, see `remote-without-sshfs` notes: never a blocking write
towards a tty client).

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
   `lookinfiles --ssh-channel` (found as `ssh_askpass` finds the program).
   It connects to the running instance on a loopback port with a 128-bit
   token (the `ssh_askpass_serve()` pattern, its own service), sends the
   machine id and the command, then copies its stdin/stdout/stderr to an
   `exec` channel the instance opened, and exits with the channel's
   status. The callers do not change: `ssh_session_argv()` returns the
   relay's argv when the link is in use. The relay must never read or
   write a secret.
4. **Terminal.** Same relay with a pty channel (`--ssh-channel --tty`):
   window size passed at start and on resize (a control message on the
   loopback socket). Optional: a terminal may also keep using `ssh.exe`
   (one prompt per terminal, acceptable). Flow control matters here: the
   instance stops reading the channel while the relay's socket is full,
   it never blocks.
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
