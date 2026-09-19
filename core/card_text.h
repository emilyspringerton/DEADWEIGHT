/* Display names and one-line rule text for the 73-card catalog (clients only; all NUMBERS live in card_rules.prn).
 * android/.../CardText.java carries the same table. */
#ifndef DW_CARD_TEXT_H
#define DW_CARD_TEXT_H
const char *dw_card_name(int id);   /* "PASS" for -1, "?" out of range */
const char *dw_card_text(int id);   /* one-line rules text, "" for the base cards and pass */
#endif
