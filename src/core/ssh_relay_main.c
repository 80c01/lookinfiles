/* Console-subsystem entry point for the Windows TTY relay
 * (lookinfiles-relay.exe, linked with -mconsole so ConPTY attaches to it). */
#include "core/ssh_relay.h"

int main(int argc, char** argv) {
#ifdef HAVE_LIBSSH
    return ssh_relay_helper_main(argc, argv);
#else
    (void)argc;
    (void)argv;
    return 255;
#endif
}
