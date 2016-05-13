#ifndef CHEAX_H
#define CHEAX_H

#include <stdio.h>
#include <stdbool.h>

typedef struct cheax CHEAX;

enum chx_value_kind {
	VK_INT, VK_DOUBLE, VK_ID, VK_CONS, VK_BUILTIN, VK_LAMBDA, VK_QUOTE
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
};

struct chx_lambda {
	struct chx_value base;
	struct chx_value *args;
	struct chx_cons *body;
	bool eval_args; /* true for (\), false for (\\) */
	/* the context in which the lambda was declared */
	struct variable *locals_top;
};

struct chx_cons *cheax_cons(struct chx_value *car, struct chx_cons *cdr);

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

/*
 * Indicates the type of a synchronized variable.
 * See also: cheax_sync()
 */
enum cheax_type {
	CHEAX_INT,
	CHEAX_FLOAT,
	CHEAX_DOUBLE,
	CHEAX_BOOL
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

struct chx_value *cheax_eval(CHEAX *c, struct chx_value *expr);
struct chx_value *cheax_read(FILE *f);
void cheax_print(FILE *output, struct chx_value *expr);

static inline void cheax_exec(CHEAX *c, FILE *f)
{
	struct chx_value *v;
	while ((v = cheax_read(f)))
		cheax_eval(c, v);
}

#endif
