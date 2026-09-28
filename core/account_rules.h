/* Declarations for the PARENA-generated account rules (core/account_rules.c, generated from
 * PARENA/stdlib/string.prn + PARENA/stdlib/deadweight/account_rules_scalar.prn +
 * PARENA/stdlib/deadweight/account_rules.prn -- do not edit the .c by hand). Curated subset a
 * real caller needs -- string.prn's own other exports (concat/split/parse-i32/etc.) are also
 * present in the .c (string.prn is combined in whole, same card_rules.h/fx_rules.h convention)
 * but have no real DEADWEIGHT caller yet, so they're not declared here; add them if/when
 * something actually needs them.
 *
 * is_valid_password_length/min_password_len (2026-09-28): the first real caller of anything in
 * this header -- apps/gui/main.c's own hand-rolled `strlen(A.link_pass) < 8` check, replaced with
 * a real call into this generated file. See account_rules_scalar.prn's own header comment for the
 * found-live duplication (main.c/MainActivity.java/account.ts all hand-copied "8 characters"
 * independently) this closes for C and Java. */
#ifndef DW_ACCOUNT_RULES_H
#define DW_ACCOUNT_RULES_H
int max_display_name_len(void);
int is_control_byte(int);
int is_valid_display_name(char *);
int min_password_len(void);
int is_valid_password_length(int);
#endif
