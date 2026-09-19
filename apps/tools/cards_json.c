/* cards_json -- dumps the card catalog (id, name, kind, Operations keyword, cost, power, credit cost, rules text) as JSON for the WOTAN deck
 * browser's card tooltips. All numbers come from the PARENA-generated card_rules; names/text from card_text.c.
 * Usage: scripts/export_cards.sh [OUT.json]  (default ../WOTAN/data/cards.json) */
#include <stdio.h>
#include <string.h>
#include "card_rules.h"
#include "card_text.h"

static void jstr(const char *s) {
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}

int main(int argc, char **argv) {
    static const char *KIND[3] = {"offense", "operations", "defense"};
    if (argc > 1 && !strcmp(argv[1], "--md")) {   /* the catalog table for docs/CARD_MODE_RULES.md */
        printf("| id | Card | Kind | Keyword | Energy | Credits | Power | What it does |\n|---|---|---|---|---|---|---|---|\n");
        for (int i = 0; i < num_cards(); i++) {
            printf("| %d | %s | %s | %s | %d | ", i, dw_card_name(i), dw_kind_name(card_kind(i)), card_keyword(i) ? dw_keyword_name(card_keyword(i)) : "-", card_cost(i));
            if (card_credit(i)) printf("%d", card_credit(i)); else printf("-");
            printf(" | %d | %s |\n", card_power(i), dw_card_text(i)[0] ? dw_card_text(i) : "(vanilla: triangle only)");
        }
        return 0;
    }
    printf("{\"version\":\"2\",\"kinds\":{\"offense\":\"Red\",\"operations\":\"Yellow\",\"defense\":\"Blue\"},\"cards\":[");
    for (int i = 0; i < num_cards(); i++) {
        printf("%s\n {\"id\":%d,\"name\":", i ? "," : "", i); jstr(dw_card_name(i));
        printf(",\"kind\":\"%s\",\"keyword\":", KIND[card_kind(i)]); jstr(dw_keyword_name(card_keyword(i)));
        printf(",\"cost\":%d,\"power\":%d,\"credit\":%d,\"text\":", card_cost(i), card_power(i), card_credit(i)); jstr(dw_card_text(i));
        printf("}");
    }
    printf("\n]}\n");
    return 0;
}
