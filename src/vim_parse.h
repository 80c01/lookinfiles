#ifndef VIM_PARSE_H
#define VIM_PARSE_H

/* Normal / Visual mode command grammar:
 *   ["x][count] command
 *   ["x][count] operator [count] (motion | text object)
 *   ["x][count] operator operator        (dd, cc, >>, g~~, gUgU...)
 * Keys are Vim keys (vim_keys.h). The parser is pure: it is given the keys
 * typed so far and says whether they form a complete command. */
#include <glib.h>

typedef enum {
    VIM_PARSE_INCOMPLETE = 0,  /* more keys needed */
    VIM_PARSE_INVALID,         /* not a command: drop the keys */
    VIM_PARSE_DONE
} VimParseStatus;

typedef enum {
    VIM_CMD_MOTION = 0,     /* move the cursor (or extend the selection) */
    VIM_CMD_OPERATOR,       /* op + motion / text object */
    VIM_CMD_OPERATOR_LINES, /* op doubled: count lines */
    VIM_CMD_VISUAL_OP,      /* op applied to the Visual selection */
    VIM_CMD_TEXTOBJ,        /* text object alone (Visual mode: extend) */
    VIM_CMD_SIMPLE          /* any other command (x, p, i, J, u, zz...) */
} VimCmdKind;

typedef struct {
    VimCmdKind kind;
    gunichar reg;       /* register ("x), 0 when not given */
    gint count;         /* product of the counts, 0 when none was typed */
    gunichar op[2];     /* operator: d c y < > = !, or g + ~ u U ? q w */
    gunichar key[2];    /* motion, text object (i/a + object) or command */
    gunichar arg;       /* character argument (f t F T r m ` ' q @), 0 if none */
    gunichar force;     /* v / V / Ctrl-V typed between operator and motion */
} VimCmd;

/* Parse keys[0..n). visual: Visual mode grammar (operators apply at once,
 * i / a start text objects). recording: q alone stops a recording. */
VimParseStatus vim_parse(const gunichar* keys, gsize n, gboolean visual,
                         gboolean recording, VimCmd* out);

/* key[0] / key[1] tests */
gboolean vim_cmd_is(const VimCmd* cmd, gunichar k0, gunichar k1);
gboolean vim_cmd_op_is(const VimCmd* cmd, gunichar o0, gunichar o1);

/* Count to use (count or 1) */
gint vim_cmd_count1(const VimCmd* cmd);

/* Is (k0, k1) a motion? Used by the engine to classify search motions. */
gboolean vim_parse_is_motion(gunichar k0, gunichar k1);

#endif /* VIM_PARSE_H */
