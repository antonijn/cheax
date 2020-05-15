#ifndef CHEAX_H
#define CHEAX_H

#include <stdio.h>
#include <stdbool.h>

typedef struct cheax CHEAX;

enum chx_value_kind {
	VK_INT, VK_DOUBLE, VK_ID, VK_CONS, VK_BUILTIN, VK_LAMBDA, VK_QUOTE, VK_PTR, VK_STRING
};

struct chx_value {
	enum chx_value_kind kind;
};

struct chx_quote {
	struct chx_value base;
	struct chx_value *value;
};

struct chx_int {
	struct chx_value base;
	int value;
};
struct chx_double {
	struct chx_value base;
	double value;
};
struct chx_ptr {
	struct chx_value base;
	void *ptr;
};

struct chx_id {
	struct chx_value base;
	char *id;
};

struct chx_cons {
	struct chx_value base;
	struct chx_value *value;
	struct chx_cons *next;
};

typedef struct chx_value *(*macro)(CHEAX *c, struct chx_cons *args);
struct chx_macro {
	struct chx_value base;
	macro perform;
	const char *name;
};

struct chx_lambda {
	struct chx_value base;
	struct chx_value *args;
	struct chx_cons *body;
	bool eval_args; /* true for (\), false for (\\) */
	/* the context in which the lambda was declared */
	struct variable *locals_top;
};

struct chx_string {
	struct chx_value base;
	char *value;
	size_t len;
};

struct chx_cons *cheax_cons(struct chx_value *car, struct chx_cons *cdr);

enum chx_error {
	/* Read errors */
	CHEAX_EREAD     = 0x0001,
	CHEAX_EEOF      = 0x0002,
	CHEAX_ELEX      = 0x0003,
	/* Eval errors */
	CHEAX_EEVAL     = 0x0101,
	CHEAX_ENOSYM    = 0x0102,
	CHEAX_ESTACK    = 0x0103,
};

/*
 * Initializes a CHEAX environment.
 */
CHEAX *cheax_init(void);
/*
 * Cleans up a CHEAX environment.
 * Calling any function on 'c' after calling this function results in
 * undefined behavior.
 */
void cheax_destroy(CHEAX *c);

enum chx_error cheax_errno(CHEAX *c);

enum cheax_builtin {
	CHEAX_FILE_IO = 1 << 0,
	CHEAX_SET_MAX_STACK_DEPTH = 1 << 1,

	CHEAX_ALL_BUILTINS = 0xFFFFFFFF,
};

void cheax_load_extra_builtins(CHEAX *c, enum cheax_builtin builtins);

/*
 * Indicates the type of a synchronized variable.
 * See also: cheax_sync()
 */
enum cheax_type {
	CHEAX_INT,
	CHEAX_FLOAT,
	CHEAX_DOUBLE,
	CHEAX_BOOL,
	CHEAX_PTR
};
/*
 * A value from CHEAX. You don't need to know what it does if you're not
 * declaring your own functions.
 */

/*
 * Synchronizes a variable in C with a name in the CHEAX environment.
 */
void cheax_sync(CHEAX *c, const char *name, enum cheax_type ty, void *var);
/*
 * Synchronize a read-only variable.
 */
void cheax_syncro(CHEAX *c, const char *name, enum cheax_type ty, const void *var);
/*
 * Synchronize, but no dumping: don't allow cheax_dump() to output this
 * variable.
 */
void cheax_syncnd(CHEAX *c, const char *name, enum cheax_type ty, void *var);

void cheax_defmacro(CHEAX *c, const char *name, macro fun);

int cheax_get_max_stack_depth(CHEAX *c);
void cheax_set_max_stack_depth(CHEAX *c, int max_stack_depth);

struct chx_value *cheax_eval(CHEAX *c, struct chx_value *expr);
struct chx_value *cheax_read(CHEAX *c, FILE *f);
struct chx_value *cheax_readstr(CHEAX *c, const char *str);
int cheax_load_prelude(CHEAX *c);
void cheax_print(FILE *output, struct chx_value *expr);

void cheax_exec(CHEAX *c, FILE *f);

#endif
