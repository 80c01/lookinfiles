"""Fake Jupyter bridge for tests/test_ipynb_kernel.c: speaks the protocol of
data/jupyter/ipynb_bridge.py without jupyter_client. Code "fail" raises,
"ask" asks for a line and prints it, "quit" makes the bridge exit, any other
code is printed back. With --list, lists two kernels and exits."""
import json
import sys


def emit(ev, **fields):
    fields["ev"] = ev
    try:
        sys.stdout.write(json.dumps(fields) + "\n")
        sys.stdout.flush()
    except BrokenPipeError:  # the test let go of the kernel
        sys.stderr.close()
        sys.exit(0)


def output(cell, out):
    emit("output", id=cell, json=json.dumps(out, sort_keys=True))


if "--list" in sys.argv[1:]:
    emit("kernels", kernels=[{"name": "fake", "display_name": "Fake", "language": "python"},
                             {"name": "ir", "display_name": "R \"4\"", "language": "R"}])
    sys.exit(0)

count = 0
waiting = None  # cell asking for a line
for line in sys.stdin:
    cmd = json.loads(line)
    op = cmd["op"]
    if op == "start":
        if cmd.get("kernel") == "missing":
            emit("error", code="no-kernel", message="No such kernel named missing")
            continue
        emit("status", state="starting")
        emit("started", kernelspec=json.dumps({"display_name": "Fake", "language": "python", "name": "fake"}),
             language_info=json.dumps({"name": "python", "file_extension": ".py"}), display="Fake")
        emit("status", state="idle")
    elif op == "execute":
        cell, code = cmd["id"], cmd["code"]
        count += 1
        emit("status", state="busy")
        emit("execute_input", id=cell, count=count)
        if code == "quit":
            sys.exit(0)
        if code == "ask":
            waiting = cell
            emit("input_request", prompt="Name? ", password=False)
            continue
        if code == "fail":
            output(cell, {"output_type": "error", "ename": "ValueError", "evalue": "bad", "traceback": ["ValueError: bad"]})
            emit("done", id=cell, status="error")
        else:
            emit("clear_output", id=cell, wait=True)
            output(cell, {"output_type": "stream", "name": "stdout", "text": code + "\n"})
            emit("done", id=cell, status="ok")
        emit("status", state="idle")
    elif op == "input":
        output(waiting, {"output_type": "stream", "name": "stdout", "text": "hello " + cmd["value"] + "\n"})
        emit("done", id=waiting, status="ok")
        emit("status", state="idle")
    elif op == "shutdown":
        break
