/* Declarations for the PARENA-generated account rules (core/account_rules.c, generated from
 * PARENA/stdlib/string.prn + PARENA/stdlib/deadweight/account_rules.prn -- do not edit the .c by
 * hand). Curated subset a real caller needs -- string.prn's own other exports (concat/split/
 * parse-i32/etc.) are also present in the .c (string.prn is combined in whole, same
 * card_rules.h/fx_rules.h convention) but have no real DEADWEIGHT caller yet, so they're not
 * declared here; add them if/when something actually needs them. */
#ifndef DW_ACCOUNT_RULES_H
#define DW_ACCOUNT_RULES_H
int max_display_name_len(void);
int is_control_byte(int);
int is_valid_display_name(char *);
#endif
