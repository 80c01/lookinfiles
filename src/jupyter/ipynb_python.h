#ifndef IPYNB_PYTHON_H
#define IPYNB_PYTHON_H

#include <glib.h>

/* The Python that runs the Jupyter bridge of a notebook: the one of the
 * notebook's virtual environment when it has Jupyter, else python3 (or
 * python) of the PATH. The virtual environment is the first .venv, venv or
 * env folder (with its pyvenv.cfg and its Python) found from the
 * notebook's folder up to the top of its repository (a folder with .git),
 * the home folder or the root. Using it is what `activate` does:
 * VIRTUAL_ENV set, its bin first in PATH, PYTHONHOME unset, so the
 * kernel, `!pip` and the programs the cells run are those of the
 * environment. A virtual environment without Jupyter is not used; its
 * folder goes to the bridge in IPYNB_PYTHON_VENV_VAR, so that its error
 * says where to install Jupyter ("no-jupyter-venv"). A Python chosen for a
 * folder (ipynb_python_choose()) wins over all that, for the notebooks of
 * the folder and of its subfolders; one in a virtual environment runs it
 * active. GLib only, no widgets; unit-tested. */

/* Set for the bridge to a virtual environment found without Jupyter */
#define IPYNB_PYTHON_VENV_VAR "LOOKIN_IPYNB_VENV"

/* The virtual environment for a notebook in folder dir (local), or NULL.
 * Caller frees. */
gchar* ipynb_python_find_venv(const gchar* dir);
/* Its Python (bin/python, Scripts\python.exe on Windows). Caller frees. */
gchar* ipynb_python_venv_python(const gchar* venv);
/* TRUE when jupyter_client is installed in it */
gboolean ipynb_python_venv_has_jupyter(const gchar* venv);
/* envp (g_get_environ() when NULL, taken) with venv active. Free with
 * g_strfreev(). */
gchar** ipynb_python_venv_environ(const gchar* venv, gchar** envp);

/* The virtual environment of a Python (venv/bin/python), or NULL. Caller frees. */
gchar* ipynb_python_venv_of(const gchar* python);

/* The Python for a local notebook in folder dir (chosen: the one chosen
 * for it, NULL: found as above; NULL when there is none) and *envp the
 * environment to run it with (NULL: this one). Caller frees both. */
gchar* ipynb_python_local(const gchar* dir, const gchar* chosen, gchar*** envp);

/* The line for a remote POSIX shell (ssh) running the bridge script with
 * chosen (a Python of the machine, ~/ for its home folder), else the Python
 * of the virtual environment of dir (a folder of the machine; NULL: none
 * looked for), else python3, else python: the arguments that follow on the
 * ssh command line go to the script (--list). Without Python it prints an
 * "error" event of code "no-python" and exits. Caller frees. */
gchar* ipynb_python_remote_command(const gchar* script, const gchar* dir, const gchar* chosen);

/* --- The Pythons chosen for folders ---
 * A key file (file: jupyter.ini of the configuration folder), a group per
 * folder (as the notebook tabs name it, a remote one too):
 *   [/home/me/project]
 *   python=/home/me/.pyenv/versions/3.12.4/bin/python
 */

/* The Python chosen for folder dir or its nearest parent, or NULL. Caller frees. */
gchar* ipynb_python_chosen(const gchar* file, const gchar* dir);
/* Choose python for folder dir (NULL or "": found again); FALSE with error
 * set when the file cannot be written or dir cannot be a group name */
gboolean ipynb_python_choose(const gchar* file, const gchar* dir, const gchar* python, GError** error);

#endif /* IPYNB_PYTHON_H */
