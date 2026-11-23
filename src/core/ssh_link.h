#ifndef SSH_LINK_H
#define SSH_LINK_H

/* One shared libssh connection per remote machine, living on a thread of
 * its own with its own GMainContext. libssh runs strictly non-blocking on
 * that thread (ssh_set_blocking(session, 0) + a level-triggered source on
 * its socket, ssh_watch.h), so a slow channel never stalls other
 * channels or the UI thread. Layer 1, no widgets; never includes
 * ssh_session.h or ssh_askpass.h. */
#include "core/ssh_channel.h"
#include "core/ssh_target.h"

#include <gio/gio.h>

#ifdef HAVE_LIBSSH

G_BEGIN_DECLS

typedef struct SshLink SshLink;
typedef struct SshLinkAsk SshLinkAsk;

typedef enum {
    SSH_LINK_ASK_SECRET,  /* password, key passphrase, or kbd-interactive secret */
    SSH_LINK_ASK_CONFIRM, /* yes / no question (unknown host key) */
    SSH_LINK_ASK_NOTICE,  /* information only (ask is NULL; no reply) */
} SshLinkAskKind;

/* Always invoked on the default main context (UI thread). Answer once with
 * ssh_link_reply() unless kind == SSH_LINK_ASK_NOTICE (where ask is NULL). */
typedef void (*SshLinkAskFunc)(SshLinkAsk* ask, SshLinkAskKind kind, const gchar* prompt, gpointer data);
/* Answer a prompt (NULL = cancelled; CONFIRM: any non-NULL text = yes) */
void ssh_link_reply(SshLinkAsk* ask, const gchar* answer);

/* Called once on the default main context: ok when connected and
 * authenticated, else error_text (UTF-8, non-NULL). */
typedef void (*SshLinkReadyFunc)(SshLink* link, gboolean ok, const gchar* error_text, gpointer data);

/* Whether libssh links are enabled for connections (TRUE on Windows unless
 * LOOKINFILES_SSH_LIBSSH=0; on Unix opt-in via LOOKINFILES_SSH_LIBSSH=1). */
gboolean ssh_link_enabled(void);

/* Connect to target on a new link thread. Returns an owning ref; close with
 * ssh_link_close() and drop with ssh_link_unref(). Once authenticated the
 * link enters the registry (ssh_link_lookup()); closing or losing it removes
 * it at once. target is copied. */
SshLink* ssh_link_connect(const SshTarget* target, SshLinkAskFunc ask, gpointer ask_data,
                          SshLinkReadyFunc ready, gpointer ready_data);
SshLink* ssh_link_ref(SshLink* link);
void ssh_link_unref(SshLink* link);
/* Close the link, leave the registry at once, fail every channel, and stop
 * the link thread. Safe to call more than once; ready() is not called after. */
void ssh_link_close(SshLink* link);
/* TRUE once connected and authenticated and not yet closed or lost */
gboolean ssh_link_is_up(const SshLink* link);

/* Find the open link for target (keyed by normalised user@host:port,
 * thread-safe). Returns a new ref or NULL. */
SshLink* ssh_link_lookup(const SshTarget* target);

/* Start opening a channel on link without blocking (returns an SshChannel
 * whose streams queue reads/writes until open, or fail if open fails).
 * Returns NULL with G_IO_ERROR_NOT_CONNECTED if link is not up. */
SshChannel* ssh_link_open_channel(SshLink* link, SshChannelKind kind, const gchar* arg,
                                  gint cols, gint rows, GError** error);
/* Open a channel and complete when the remote channel request succeeds */
void ssh_link_open_channel_async(SshLink* link, SshChannelKind kind, const gchar* arg,
                                 gint cols, gint rows, GCancellable* cancel,
                                 GAsyncReadyCallback done, gpointer data);
SshChannel* ssh_link_open_channel_finish(SshLink* link, GAsyncResult* result, GError** error);
/* Synchronous open for worker threads only (refused on the main thread) */
SshChannel* ssh_link_open_channel_sync(SshLink* link, SshChannelKind kind, const gchar* arg,
                                       gint cols, gint rows, GCancellable* cancel, GError** error);

/* Test configuration hooks (pass NULL / TRUE / TRUE / 0 to restore defaults) */
void ssh_link_set_test_options(const gchar* config_file, const gchar* known_hosts,
                               gboolean try_agent, gboolean try_default_keys);
void ssh_link_set_keepalive_ms(guint interval_ms);

G_END_DECLS

#endif /* HAVE_LIBSSH */

#endif /* SSH_LINK_H */
