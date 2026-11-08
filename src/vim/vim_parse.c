/* Vim Normal / Visual command grammar */
#include "vim/vim_parse.h"
#include "vim/vim_keys.h"
#include <string.h>

/* Keys standing for other keys in Normal / Visual mode */
static gunichar alias(gunichar k) {
    switch (k) {
        case VIM_KEY_LEFT: case VIM_KEY_BS: return 'h';
        case VIM_KEY_RIGHT: case ' ': return 'l';
        case VIM_KEY_UP: case VIM_KEY_CTRL('p'): return 'k';
        case VIM_KEY_DOWN: case VIM_KEY_NL: case VIM_KEY_CTRL('n'): return 'j';
        case VIM_KEY_END: return '$';
        case VIM_KEY_CR: return '+';
        case VIM_KEY_DEL: return 'x';
        case VIM_KEY_INSERT: return 'i';
        case VIM_KEY_PAGEDOWN: return VIM_KEY_CTRL('f');
        case VIM_KEY_PAGEUP: return VIM_KEY_CTRL('b');
        default: return k;
    }
}

static gboolean in_set(gunichar k, const gchar* set) {
    return k != 0 && k < 128 && strchr(set, (gchar)k) != NULL;
}

gboolean vim_parse_is_motion(gunichar k0, gunichar k1) {
    if (k1 == 0) return in_set(k0, "hjklwWbBeE0^$G%{}()HML+-_|nN*#;,/?fFtT`'");
    if (k0 == 'g') return in_set(k1, "geE_jk0^$mM*#dDo");
    if (k0 == '[') return in_set(k1, "({[]");
    if (k0 == ']') return in_set(k1, ")}[]");
    return FALSE;
}

/* A motion at keys[pos]: consumed keys or 0 (*status tells why) */
static gsize parse_motion(const gunichar* keys, gsize pos, gsize n, VimCmd* out,
                          VimParseStatus* status) {
    *status = VIM_PARSE_INVALID;
    gunichar k = alias(keys[pos]);
    if (k == VIM_KEY_HOME) k = '0';
    if (in_set(k, "fFtT`'")) {
        if (pos + 1 >= n) {
            *status = VIM_PARSE_INCOMPLETE;
            return 0;
        }
        gunichar a = keys[pos + 1];
        if (a == VIM_KEY_ESC) return 0;
        out->key[0] = k;
        out->key[1] = 0;
        out->arg = a;
        *status = VIM_PARSE_DONE;
        return 2;
    }
    if (k == 'g' || k == '[' || k == ']') {
        if (pos + 1 >= n) {
            *status = VIM_PARSE_INCOMPLETE;
            return 0;
        }
        gunichar k1 = keys[pos + 1];
        if (!vim_parse_is_motion(k, k1)) return 0;
        out->key[0] = k;
        out->key[1] = k1;
        *status = VIM_PARSE_DONE;
        return 2;
    }
    if (vim_parse_is_motion(k, 0)) {
        out->key[0] = k;
        out->key[1] = 0;
        *status = VIM_PARSE_DONE;
        return 1;
    }
    return 0;
}

static gsize parse_textobj(const gunichar* keys, gsize pos, gsize n, VimCmd* out,
                           VimParseStatus* status) {
    *status = VIM_PARSE_INVALID;
    gunichar k = keys[pos];
    if (k != 'i' && k != 'a') return 0;
    if (pos + 1 >= n) {
        *status = VIM_PARSE_INCOMPLETE;
        return 0;
    }
    gunichar o = keys[pos + 1];
    if (!in_set(o, "wWsp()b[]{}B<>\"'`t")) return 0;
    out->key[0] = k;
    out->key[1] = o;
    *status = VIM_PARSE_DONE;
    return 2;
}

/* Operator at keys[pos] */
static gsize parse_operator(const gunichar* keys, gsize pos, gsize n, gunichar op[2],
                            VimParseStatus* status) {
    *status = VIM_PARSE_INVALID;
    gunichar k = keys[pos];
    if (in_set(k, "dcy<>=")) {
        op[0] = k;
        op[1] = 0;
        *status = VIM_PARSE_DONE;
        return 1;
    }
    if (k == 'g') {
        if (pos + 1 >= n) {
            *status = VIM_PARSE_INCOMPLETE;
            return 0;
        }
        if (in_set(keys[pos + 1], "~uU?qw")) {
            op[0] = 'g';
            op[1] = keys[pos + 1];
            *status = VIM_PARSE_DONE;
            return 2;
        }
    }
    return 0;
}

static gsize simple_done(VimCmd* out, VimParseStatus* status, gunichar k0, gunichar k1,
                         gunichar arg, gsize used) {
    out->key[0] = k0;
    out->key[1] = k1;
    out->arg = arg;
    *status = VIM_PARSE_DONE;
    return used;
}

static gsize parse_simple(const gunichar* keys, gsize pos, gsize n, gboolean visual,
                          gboolean recording, VimCmd* out, VimParseStatus* status) {
    *status = VIM_PARSE_INVALID;
    gunichar k = alias(keys[pos]);
    gboolean more = pos + 1 < n;
    gunichar k1 = more ? keys[pos + 1] : 0;

    /* Commands taking a character */
    gboolean takes_arg = (k == 'r' || k == 'm' || k == '@' || (k == 'q' && !recording));
    if (visual && (k == 'm' || k == 'q' || k == '@')) takes_arg = k != 'q' || !recording;
    if (takes_arg) {
        if (!more) {
            *status = VIM_PARSE_INCOMPLETE;
            return 0;
        }
        if (k1 == VIM_KEY_ESC) return 0;
        return simple_done(out, status, k, 0, k1, 2);
    }
    if (k == 'g' || k == 'z' || k == 'Z') {
        if (!more) {
            *status = VIM_PARSE_INCOMPLETE;
            return 0;
        }
        const gchar* set = k == 'g' ? (visual ? "Jv~uU?qwJ" : "JpPviI&tTa8r")
                         : k == 'z' ? "ztb.-"
                                    : "ZQ";
        if (in_set(k1, set) || (k == 'z' && k1 == VIM_KEY_CR) ||
            (k == 'g' && k1 == VIM_KEY_CTRL('g')) ||
            (k == 'g' && visual && (k1 == VIM_KEY_CTRL('a') || k1 == VIM_KEY_CTRL('x')))) {
            return simple_done(out, status, k, k1, 0, 2);
        }
        return 0;
    }
    static const gunichar CTRL_KEYS[] = {
        VIM_KEY_CTRL('r'), VIM_KEY_CTRL('a'), VIM_KEY_CTRL('x'), VIM_KEY_CTRL('e'),
        VIM_KEY_CTRL('y'), VIM_KEY_CTRL('d'), VIM_KEY_CTRL('u'), VIM_KEY_CTRL('f'),
        VIM_KEY_CTRL('b'), VIM_KEY_CTRL('o'), VIM_KEY_TAB, VIM_KEY_CTRL('g'),
        VIM_KEY_CTRL('l'), VIM_KEY_CTRL('v'), VIM_KEY_CTRL('c'), VIM_KEY_ESC,
        VIM_KEY_CTRL(']'),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(CTRL_KEYS); i++) {
        if (k == CTRL_KEYS[i]) return simple_done(out, status, k, 0, 0, 1);
    }
    const gchar* set = visual ? "oOIAJ~uUpPxXdDyYcCsSR<>=:vV&"
                              : "xXsSCDYpPJR~uU.iIaAoOvV:&qK";
    if (in_set(k, set)) return simple_done(out, status, k, 0, 0, 1);
    return 0;
}

static gint add_digit(gint count, gunichar d) {
    if (count > 99999999) return count;
    return count * 10 + (gint)(d - '0');
}

VimParseStatus vim_parse(const gunichar* keys, gsize n, gboolean visual,
                         gboolean recording, VimCmd* out) {
    memset(out, 0, sizeof(*out));
    gsize pos = 0;
    gint count1 = 0;
    /* Register and count, in any order */
    for (;;) {
        if (pos >= n) return VIM_PARSE_INCOMPLETE;
        gunichar k = keys[pos];
        if (k == '"') {
            if (pos + 1 >= n) return VIM_PARSE_INCOMPLETE;
            gunichar r = keys[pos + 1];
            if (r == VIM_KEY_ESC) return VIM_PARSE_INVALID;
            out->reg = r;
            pos += 2;
            continue;
        }
        if (k >= '1' && k <= '9') {
            count1 = add_digit(count1, k);
            pos++;
            continue;
        }
        if (k == '0' && count1 > 0) {
            count1 = add_digit(count1, k);
            pos++;
            continue;
        }
        break;
    }
    out->count = count1;
    VimParseStatus st;
    gsize used;

    if (visual) {
        used = parse_textobj(keys, pos, n, out, &st);
        if (st == VIM_PARSE_INCOMPLETE) return st;
        if (st == VIM_PARSE_DONE) {
            out->kind = VIM_CMD_TEXTOBJ;
            return pos + used == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
        }
        used = parse_motion(keys, pos, n, out, &st);
        if (st == VIM_PARSE_DONE) {
            out->kind = VIM_CMD_MOTION;
            return pos + used == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
        }
        VimParseStatus st2;
        used = parse_simple(keys, pos, n, TRUE, recording, out, &st2);
        if (st2 == VIM_PARSE_DONE) {
            out->kind = VIM_CMD_SIMPLE;
            return VIM_PARSE_DONE;
        }
        return (st == VIM_PARSE_INCOMPLETE || st2 == VIM_PARSE_INCOMPLETE)
                   ? VIM_PARSE_INCOMPLETE : VIM_PARSE_INVALID;
    }

    /* Operator pending */
    gunichar op[2] = {0, 0};
    used = parse_operator(keys, pos, n, op, &st);
    if (st == VIM_PARSE_INCOMPLETE) {
        /* "g" alone may also start a g-motion or a g-command */
        return VIM_PARSE_INCOMPLETE;
    }
    if (st == VIM_PARSE_DONE) {
        out->op[0] = op[0];
        out->op[1] = op[1];
        gsize p = pos + used;
        gint count2 = 0;
        for (;;) {
            if (p >= n) return VIM_PARSE_INCOMPLETE;
            gunichar k = keys[p];
            if ((k >= '1' && k <= '9') || (k == '0' && count2 > 0)) {
                count2 = add_digit(count2, k);
                p++;
                continue;
            }
            if (k == 'v' || k == 'V' || k == VIM_KEY_CTRL('v')) {
                out->force = k;
                p++;
                continue;
            }
            break;
        }
        if (count2 > 0) out->count = count1 > 0 ? count1 * count2 : count2;
        if (keys[p] == VIM_KEY_ESC) return VIM_PARSE_INVALID;
        /* Doubled: dd, >>, g~~ / g~g~ */
        if (op[1] == 0 && keys[p] == op[0]) {
            out->kind = VIM_CMD_OPERATOR_LINES;
            return p + 1 == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
        }
        if (op[1] != 0) {
            if (keys[p] == op[1]) {
                out->kind = VIM_CMD_OPERATOR_LINES;
                return p + 1 == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
            }
            if (keys[p] == 'g') {
                if (p + 1 >= n) return VIM_PARSE_INCOMPLETE;
                if (keys[p + 1] == op[1]) {
                    out->kind = VIM_CMD_OPERATOR_LINES;
                    return p + 2 == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
                }
            }
        }
        used = parse_textobj(keys, p, n, out, &st);
        if (st == VIM_PARSE_INCOMPLETE) return st;
        if (st == VIM_PARSE_DONE) {
            out->kind = VIM_CMD_OPERATOR;
            return p + used == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
        }
        used = parse_motion(keys, p, n, out, &st);
        if (st == VIM_PARSE_DONE) {
            out->kind = VIM_CMD_OPERATOR;
            return p + used == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
        }
        return st;
    }

    used = parse_motion(keys, pos, n, out, &st);
    if (st == VIM_PARSE_DONE) {
        out->kind = VIM_CMD_MOTION;
        return pos + used == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
    }
    VimParseStatus st2;
    used = parse_simple(keys, pos, n, FALSE, recording, out, &st2);
    if (st2 == VIM_PARSE_DONE) {
        out->kind = VIM_CMD_SIMPLE;
        return pos + used == n ? VIM_PARSE_DONE : VIM_PARSE_INVALID;
    }
    if (st == VIM_PARSE_INCOMPLETE || st2 == VIM_PARSE_INCOMPLETE) return VIM_PARSE_INCOMPLETE;
    return VIM_PARSE_INVALID;
}

gboolean vim_cmd_is(const VimCmd* cmd, gunichar k0, gunichar k1) {
    return cmd->key[0] == k0 && cmd->key[1] == k1;
}

gboolean vim_cmd_op_is(const VimCmd* cmd, gunichar o0, gunichar o1) {
    return cmd->op[0] == o0 && cmd->op[1] == o1;
}

gint vim_cmd_count1(const VimCmd* cmd) {
    return cmd->count > 0 ? cmd->count : 1;
}
