/* cards_json -- dumps the 73-card catalog (id, name, kind, cost, power, credit cost, rules text) as JSON for the WOTAN deck
 * browser's card tooltips. All numbers come from the PARENA-generated card_rules; names/text from card_text.c.
 * Usage: scripts/export_cards.sh [OUT.json]  (default ../WOTAN/data/cards.json) */
#include <stdio.h>
#include "card_rules.h"
#include "card_text.h"

static void jstr(const char *s) {
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}

int main(void) {
    static const char *KIND[3] = {"burst", "tank", "shield"};
    printf("{\"version\":\"%s\",\"cards\":[", "1");
    for (int i = 0; i < num_cards(); i++) {
        printf("%s\n {\"id\":%d,\"name\":", i ? "," : "", i); jstr(dw_card_name(i));
        printf(",\"kind\":\"%s\",\"cost\":%d,\"power\":%d,\"credit\":%d,\"text\":", KIND[card_kind(i)], card_cost(i), card_power(i), card_credit(i)); jstr(dw_card_text(i));
        printf("}");
    }
    printf("\n]}\n");
    return 0;
}
