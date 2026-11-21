#ifndef SYSSTAT_REMOTE_H
#define SYSSTAT_REMOTE_H

/* System statistics of a remote Linux machine, read by one shell command
 * over ssh (ssh_exec_async()): the command prints the /proc files and a few
 * other readings in sections ("@@<name>" lines), and the parser gives their
 * text to the parsers of sysstat.h, so a remote sample is a SysSample like a
 * local one. Its time is the machine's uptime, so the rates do not depend on
 * the network delay. Pure (unit-tested). */
#include "core/sysstat.h"

/* The line for the remote shell; flags as sysstat_sample() (the processes,
 * the filesystems). Caller frees. */
gchar* sysstat_remote_command(SysSampleFlags flags);

/* A sample from the output of that command (sysstat_sample_rates() and
 * sysstat_sample_free() apply), NULL when it holds no /proc/stat. *host, *os
 * and *page_size (each may be NULL) get the machine's name, its system
 * ("Linux 6.8.0-45-generic") and its memory page size. */
SysSample* sysstat_remote_parse(const gchar* text, SysSampleFlags flags, gchar** host, gchar** os,
                                guint64* page_size);

#endif /* SYSSTAT_REMOTE_H */
