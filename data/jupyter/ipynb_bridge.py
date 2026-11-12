"""Jupyter bridge of LookinFiles (src/jupyter/ipynb_kernel.h).

Runs one Jupyter kernel with jupyter_client and speaks to the application in
JSON, one object per line: commands on stdin, events on stdout. Bundled in
the binary and run as `python3 -c <this file>`; `python3 -c <this file>
--list` only lists the installed kernels (a "kernels" event) and exits.

Commands (field "op"):
  start      kernel (name, "" for the default), cwd
  execute    id (the cell), code
  input      value (answer to an input_request)
  interrupt, restart, shutdown

Events (field "ev"):
  kernels        kernels (a list of {name, display_name, language})
  started        kernelspec, language_info (JSON texts), display
  error          code ("no-jupyter", "no-kernel", "start", "command"), message
  status         state ("starting", "busy", "idle", "dead")
  execute_input  id, count
  output         id, json (an nbformat output object, as JSON text)
  clear_output   id, wait
  done           id, status ("ok", "error", "aborted")
  input_request  prompt, password
  restarted
"""
import json
import os
import queue
import sys
import threading

# The protocol has stdout to itself: the kernel and the libraries write to
# stderr (fd 1 is stderr from here on, for this process and its children)
_PROTO = os.fdopen(os.dup(1), "w", encoding="utf-8", buffering=1)
os.dup2(2, 1)
sys.stdout = sys.stderr
_LOCK = threading.Lock()
_STOP = threading.Event()


def emit(ev, **fields):
    fields["ev"] = ev
    line = json.dumps(fields, ensure_ascii=False)
    with _LOCK:
        try:
            _PROTO.write(line + "\n")
            _PROTO.flush()
        except (BrokenPipeError, OSError, ValueError):
            # The application is gone: its end of stdin follows, then the
            # kernel is shut down
            _STOP.set()


class Bridge:
    def __init__(self, manager_class):
        self.manager_class = manager_class
        self.km = None
        self.kc = None
        self.cells = {}      # execute request msg_id -> cell id
        self.finished = set()
        self.errors = set()
        self.lock = threading.Lock()
        self.dead = False
        self.restarting = False  # until the restarted kernel answers

    # --- Replies of the kernel ---

    def cell_of(self, msg):
        """The request a reply is for, and its cell (None once the cell is
        done: what comes after belongs to no cell, e.g. the interrupt of a
        cell aborted by a restart)"""
        parent = msg.get("parent_header") or {}
        msg_id = parent.get("msg_id")
        with self.lock:
            if msg_id in self.finished:
                return msg_id, None
            return msg_id, self.cells.get(msg_id)

    def finish(self, msg_id, status):
        with self.lock:
            cell = self.cells.get(msg_id)
            if cell is None or msg_id in self.finished:
                return
            self.finished.add(msg_id)
            if msg_id in self.errors and status == "ok":
                status = "error"
        emit("done", id=cell, status=status)

    def iopub_loop(self):
        while not _STOP.is_set():
            try:
                msg = self.kc.get_iopub_msg(timeout=0.5)
            except queue.Empty:
                alive = self.km.is_alive()
                if not alive and not self.dead:
                    self.dead = True
                    emit("status", state="dead")
                elif alive:
                    self.dead = False
                continue
            except Exception:  # channels closed
                return
            kind = msg["header"]["msg_type"]
            content = msg.get("content") or {}
            msg_id, cell = self.cell_of(msg)
            if kind == "status":
                state = content.get("execution_state")
                if state in ("starting", "busy", "idle"):
                    emit("status", state=state)
                if state == "idle" and cell is not None:
                    self.finish(msg_id, "ok")
                continue
            if cell is None:
                continue
            if kind == "execute_input":
                emit("execute_input", id=cell, count=content.get("execution_count"))
            elif kind == "stream":
                self.output(cell, {"output_type": "stream", "name": content.get("name", "stdout"),
                                   "text": content.get("text", "")})
            elif kind in ("execute_result", "display_data"):
                out = {"output_type": kind, "data": content.get("data", {}),
                       "metadata": content.get("metadata", {})}
                if kind == "execute_result":
                    out["execution_count"] = content.get("execution_count")
                self.output(cell, out)
            elif kind == "error":
                with self.lock:
                    self.errors.add(msg_id)
                self.output(cell, {"output_type": "error", "ename": content.get("ename", ""),
                                   "evalue": content.get("evalue", ""),
                                   "traceback": content.get("traceback", [])})
            elif kind == "clear_output":
                emit("clear_output", id=cell, wait=bool(content.get("wait", False)))

    def output(self, cell, out):
        emit("output", id=cell, json=json.dumps(out, sort_keys=True, ensure_ascii=False))

    def shell_loop(self):
        while not _STOP.is_set():
            try:
                msg = self.kc.get_shell_msg(timeout=0.5)
            except queue.Empty:
                continue
            except Exception:
                return
            if msg["header"]["msg_type"] == "kernel_info_reply" and self.restarting:
                self.restarting = False
                emit("status", state="idle")
                continue
            if msg["header"]["msg_type"] != "execute_reply":
                continue
            msg_id, cell = self.cell_of(msg)
            status = (msg.get("content") or {}).get("status")
            if status == "aborted":
                self.finish(msg_id, "aborted")
            elif status == "error":
                with self.lock:
                    self.errors.add(msg_id)

    def stdin_loop(self):
        while not _STOP.is_set():
            try:
                msg = self.kc.get_stdin_msg(timeout=0.5)
            except queue.Empty:
                continue
            except Exception:
                return
            if msg["header"]["msg_type"] == "input_request":
                content = msg.get("content") or {}
                emit("input_request", prompt=content.get("prompt", ""),
                     password=bool(content.get("password", False)))

    # --- Commands ---

    def start(self, name, cwd):
        if self.km is not None:
            return
        try:
            self.km = self.manager_class(kernel_name=name) if name else self.manager_class()
            emit("status", state="starting")
            self.km.start_kernel(cwd=cwd or None)
        except Exception as e:  # NoSuchKernel, a kernel that cannot start
            code = "no-kernel" if type(e).__name__ == "NoSuchKernel" else "start"
            self.km = None
            emit("error", code=code, message=str(e) or type(e).__name__)
            return
        self.kc = self.km.client()
        self.kc.start_channels()
        try:
            self.kc.wait_for_ready(timeout=60)
            reply = self.kc.kernel_info(reply=True, timeout=30)
            language_info = (reply.get("content") or {}).get("language_info", {})
        except Exception as e:
            emit("error", code="start", message=str(e) or type(e).__name__)
            return
        spec = self.km.kernel_spec
        kernelspec = {"display_name": spec.display_name if spec else self.km.kernel_name,
                      "language": spec.language if spec else language_info.get("name", ""),
                      "name": self.km.kernel_name}
        for loop in (self.iopub_loop, self.shell_loop, self.stdin_loop):
            threading.Thread(target=loop, daemon=True).start()
        emit("started", kernelspec=json.dumps(kernelspec, sort_keys=True, ensure_ascii=False),
             language_info=json.dumps(language_info, sort_keys=True, ensure_ascii=False),
             display=kernelspec["display_name"])
        emit("status", state="idle")

    def wait_restarted(self):
        """Ask the restarted kernel who it is until it answers (the shell
        loop then says it is idle): it does not say so by itself"""
        for _ in range(120):
            if not self.restarting or _STOP.is_set():
                return
            self.kc.kernel_info()
            _STOP.wait(0.5)

    def abort_all(self):
        with self.lock:
            pending = [m for m in self.cells if m not in self.finished]
        for msg_id in pending:
            self.finish(msg_id, "aborted")

    def command(self, cmd):
        op = cmd.get("op")
        if op == "start":
            self.start(cmd.get("kernel") or "", cmd.get("cwd") or "")
            return True
        if op == "shutdown":
            return False
        if self.kc is None:
            if op == "execute":
                emit("done", id=cmd.get("id"), status="aborted")
            return True
        if op == "execute":
            msg_id = self.kc.execute(cmd.get("code", ""), allow_stdin=True, stop_on_error=True)
            with self.lock:
                self.cells[msg_id] = cmd.get("id")
        elif op == "input":
            self.kc.input(cmd.get("value", ""))
        elif op == "interrupt":
            self.km.interrupt_kernel()
        elif op == "restart":
            self.abort_all()
            self.restarting = True
            self.km.restart_kernel(now=False)
            emit("restarted")
            threading.Thread(target=self.wait_restarted, daemon=True).start()
        return True

    def shutdown(self):
        _STOP.set()
        self.abort_all()
        try:
            if self.kc is not None:
                self.kc.stop_channels()
            if self.km is not None:
                self.km.shutdown_kernel(now=True)
        except Exception:
            pass


def list_kernels():
    from jupyter_client.kernelspec import KernelSpecManager
    try:
        specs = KernelSpecManager().get_all_specs()
    except Exception as e:
        emit("error", code="command", message=str(e) or type(e).__name__)
        return 1
    kernels = []
    for name, info in specs.items():
        spec = (info or {}).get("spec") or {}
        kernels.append({"name": name, "display_name": spec.get("display_name") or name,
                        "language": spec.get("language") or ""})
    kernels.sort(key=lambda k: (k["display_name"].lower(), k["name"]))
    emit("kernels", kernels=kernels)
    return 0


def main():
    try:
        from jupyter_client import KernelManager
    except ImportError:
        emit("error", code="no-jupyter", message="jupyter_client is not installed")
        return 1
    if "--list" in sys.argv[1:]:
        return list_kernels()
    bridge = Bridge(KernelManager)
    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                if not bridge.command(json.loads(line)):
                    break
            except Exception as e:
                emit("error", code="command", message=str(e) or type(e).__name__)
    finally:
        # Asked to, or the application is gone (end of stdin): the kernel too
        bridge.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
