/* dw_server -- DEADWEIGHT match server. Skeleton (S503-02): builds, links the PARENA-generated rules, answers
 * --version. The real poll() loop / matchmaker / matches land in S503-04 against docs/WIRE_PROTOCOL.md. */
#include <stdio.h>
#include <string.h>
#include "card_rules.h"
#include "version.h"

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) {
        printf("dw_server %s (rules: %d cards, hull %d)\n", DW_VERSION, num_cards(), start_hull());
        return 0;
    }
    fprintf(stderr, "dw_server %s: server loop not implemented yet (S503-04)\n", DW_VERSION);
    return 2;
}
