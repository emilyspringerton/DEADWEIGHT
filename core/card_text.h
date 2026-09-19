/* Display names and one-line rule text for the card catalog (clients only; all NUMBERS live in card_rules.prn).
 * android/.../CardText.java is generated from this table by apps/tools/cards_java.c (scripts/gen_rules.sh). */
#ifndef DW_CARD_TEXT_H
#define DW_CARD_TEXT_H
const char *dw_card_name(int id);      /* "PASS" for -1, "?" out of range */
const char *dw_card_text(int id);      /* one-line rules text, "" for the vanilla base cards and pass */
const char *dw_kind_name(int kind);    /* 0 "Offense" (Red), 1 "Operations" (Yellow), 2 "Defense" (Blue) */
const char *dw_keyword_name(int kw);   /* card_keyword(): "" for none, else Lock / Sabotage / Flank / Scan / Siphon */
#endif
