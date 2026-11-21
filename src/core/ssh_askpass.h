#ifndef SSH_ASKPASS_H
#define SSH_ASKPASS_H

/* Password and passphrase prompts of ssh shown by the application. ssh
 * started with ssh_session.h runs SSH_ASKPASS when it needs a secret: that is
 * this program again, in helper mode (ssh_askpass_is_helper(), before any
 * GTK call in main()), which hands the prompt to the running instance over a
 * loopback TCP connection guarded by a random token, and prints the answer
 * for ssh. TCP rather than a Unix socket so that it works on Windows too.
 * GIO only, no widgets (the instance shows the prompt in its ask function);
 * unit-tested. */
#include <gio/gio.h>

/* Variable naming the instance to ask, "<port>:<token>" */
#define SSH_ASKPASS_ENV "LOOKINFILES_ASKPASS"

typedef enum {
    SSH_ASKPASS_SECRET,  /* a password, passphrase or PIN to type */
    SSH_ASKPASS_CONFIRM, /* a yes / no question (SSH_ASKPASS_PROMPT=confirm) */
    SSH_ASKPASS_NOTICE,  /* information only (SSH_ASKPASS_PROMPT=none): answered at once */
} SshAskpassKind;

typedef struct SshAskpassRequest SshAskpassRequest;

/* A prompt to show; answer it once with ssh_askpass_reply() (later is fine).
 * request is NULL for SSH_ASKPASS_NOTICE (already answered). */
typedef void (*SshAskpassFunc)(SshAskpassRequest* request, SshAskpassKind kind, const gchar* prompt,
                               gpointer data);
/* Send the answer (NULL: cancelled; CONFIRM: any text is yes) and free request */
void ssh_askpass_reply(SshAskpassRequest* request, const gchar* answer);

/* Listen for the prompts of the ssh programs started from now on and set
 * their SSH_ASKPASS variables (ssh_session_set_env()); program is the path
 * of this executable. */
gboolean ssh_askpass_serve(const gchar* program, SshAskpassFunc ask, gpointer data, GError** error);
/* "<port>:<token>" of the instance listening, NULL when not serving */
const gchar* ssh_askpass_address(void);
/* Stop listening and unset the variables */
void ssh_askpass_stop(void);

/* Whether this process was started by ssh as its SSH_ASKPASS program */
gboolean ssh_askpass_is_helper(int argc, char** argv);
/* The helper: ask the instance, print the answer; the exit status for ssh */
int ssh_askpass_helper_main(int argc, char** argv);
/* Ask the instance at address ("<port>:<token>"). TRUE with *answer set
 * (caller frees) when answered, FALSE when cancelled or unreachable (error
 * set then). Blocks. */
gboolean ssh_askpass_request(const gchar* address, SshAskpassKind kind, const gchar* prompt,
                             gchar** answer, GError** error);

#endif /* SSH_ASKPASS_H */
