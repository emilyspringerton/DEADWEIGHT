/* dw_client -- DEADWEIGHT headless client (Windows/Linux). Skeleton (S503-02); real client lands in S503-09. */
#include <stdio.h>
#include <string.h>
#include "card_rules.h"
#include "version.h"

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) {
        printf("dw_client %s (rules: %d cards, hull %d)\n", DW_VERSION, num_cards(), start_hull());
        return 0;
    }
    fprintf(stderr, "dw_client %s: client not implemented yet (S503-09)\n", DW_VERSION);
    return 2;
}
