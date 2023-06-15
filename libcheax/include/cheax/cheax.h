/*!
 * \defgroup Cheax The cheax C API
 * \brief API functions to interface with cheax from C/C++.
 * @{
 */

/*! \file cheax.h
 * \brief The header for the cheax C API.
 */
#ifndef CHEAX_H
#define CHEAX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <cheax/export.h>

/*! \brief The type of the cheax virtual machine state, a pointer to
 *         wich is needed for most cheax API functions.
 *
 * The virtual machine is initialized with cheax_init(), and destroyed
 * with cheax_destroy().
 *
 * \sa cheax_init(), cheax_destroy(), cheax_load_features(),
 *     cheax_load_prelude()
 */
typedef struct cheax CHEAX;

/*!
 * \defgroup TypeSystem Type system
 * \brief Functions and datastructures to create, examine and manipulate
 * cheax expressions and their types.
 * @{
 */

/*! \brief Types of expressions within cheax.
 * \sa cheax_new_type(), chx_value::type
 */
enum {
	CHEAX_LIST,          /*!< List type code. */
	CHEAX_INT,           /*!< Integral type code. */
	CHEAX_BOOL,          /*!< Boolean type code. */
	CHEAX_DOUBLE,        /*!< Floating point type code. */

	/*! \brief Type of user pointers defined from outside the cheax
	 * environment.
	 *
	 * \note Objects of this basic type exist only through type
	 * aliases made with cheax_new_type(), and do not exist as
	 * 'bare' user pointers.
	 */
	CHEAX_USER_PTR,

	CHEAX_ID,            /*!< Identifier type code. */
	CHEAX_FUNC,          /*!< Function type code. */
	CHEAX_EXT_FUNC,      /*!< Type code for functions defined through the C API. */
	CHEAX_SPECIAL_OP,    /*!< Type code for special operations, defined through the C API. */
	CHEAX_QUOTE,         /*!< Type for quoted expressions. */
	CHEAX_BACKQUOTE,     /*!< Type for backquoted expressions. */
	CHEAX_COMMA,         /*!< Type for comma expressions. */
	CHEAX_SPLICE,        /*!< Type for comma splice (i.e. ,@) expressions. */
	CHEAX_STRING,        /*!< String type. */
	CHEAX_ENV,           /*!< Environment type. */

	CHEAX_LAST_BASIC_TYPE = CHEAX_ENV,
	CHEAX_TYPESTORE_BIAS,

	/*! The type of type codes themselves. A type alias of \ref CHEAX_INT. */
	CHEAX_TYPECODE = CHEAX_TYPESTORE_BIAS + 0,
	CHEAX_ERRORCODE,     /*!< Error code type. A type alias of \ref CHEAX_INT. */
};

/*! \brief Minimum value for \ref chx_int. */
#define CHX_INT_MIN INT_LEAST64_MIN
/*! \brief Maximum value for \ref chx_int. */
#define CHX_INT_MAX INT_LEAST64_MAX

/*! \brief Integer type.
 * \sa CHEAX_INT, cheax_int(), CHX_INT_MIN, CHX_INT_MAX, chx_value
 */
typedef int_least64_t chx_int;

/*! \brief Floating point type.
 * \sa CHEAX_DOUBLE, cheax_double(), chx_value
 */
typedef double chx_double;

struct chx_list;
struct chx_id;
struct chx_string;
struct chx_quote;
struct chx_func;
struct chx_ext_func;
struct chx_special_op;
struct chx_env;

/*! \brief Represents a value in the cheax environment.
 *
 * Consists of a tuple of the value's type and the value's data. */
struct chx_value {
	int type; /*!< Type code. Indicates how to intepret chx_value::data field. */
#if __STDC_VERSION__ + 0 >= 201112L
	union {
		chx_int as_int;
		chx_double as_double;
		struct chx_list *as_list;
		struct chx_id *as_id;
		struct chx_string *as_string;
		struct chx_quote *as_quote;
		struct chx_func *as_func;
		struct chx_ext_func *as_ext_func;
		struct chx_special_op *as_special_op;
		struct chx_env *as_env;
		void *user_ptr;

		unsigned *rtflags_ptr;
#endif

	/*! \brief Data stored in the value. */
	union {
		/*! \brief Data when type is \ref CHEAX_INT or \ref CHEAX_BOOL. */
		chx_int as_int;
		/*! \brief Data when type is \ref CHEAX_DOUBLE. */
		chx_double as_double;
		/*! \brief Data when type is \ref CHEAX_LIST. */
		struct chx_list *as_list;
		/*! \brief Data when type is \ref CHEAX_ID. */
		struct chx_id *as_id;
		/*! \brief Data when type is \ref CHEAX_STRING. */
		struct chx_string *as_string;
		/*! \brief Data when type is \ref CHEAX_QUOTE, \ref CHEAX_COMMA
		 *         or \ref CHEAX_SPLICE.. */
		struct chx_quote *as_quote;
		/*! \brief Data when type is \ref CHEAX_FUNC. */
		struct chx_func *as_func;
		/*! \brief Data when type is \ref CHEAX_SPECIAL_OP or
		 *         \ref CHEAX_EXT_FUNC. */
		struct chx_ext_func *as_ext_func;
		struct chx_special_op *as_special_op;
		/*! \brief Data when type is \ref CHEAX_ENV. */
		struct chx_env *as_env;
		/*! \brief Data when type is \ref CHEAX_USER_PTR. */
		void *user_ptr;

		/*! \brief Runtime flags. \note For internal use. */
		unsigned *rtflags_ptr;
	} data;
#if __STDC_VERSION__ + 0 >= 201112L
	};
#endif
};

/*! \brief The `nil` value. \note Requires C99 to use. */
#define CHEAX_NIL ((struct chx_value){ 0 })

/*! \brief Creates a `nil` value. \deprecated In favor of \ref CHEAX_NIL. */
CHX_API struct chx_value cheax_nil(void);

/*! \brief Tests whether given value is `nil`.
 *
 * \returns Whether \a v.type is \ref CHEAX_LIST and \a v.data.as_list is `NULL`.
 */
CHX_API bool cheax_is_nil(struct chx_value v);

/*! \brief Identifier type.
 */
struct chx_id {
	unsigned rtflags;       /*!< Runtime flags. \note For internal use. */
	char *value;            /*!< Null-terminated value. */
};

/*! \brief Quoted value type.
 *
 * Also used for values of type \ref CHEAX_BACKQUOTE, \ref CHEAX_COMMA
 * and \ref CHEAX_SPLICE.
 */
struct chx_quote {
	unsigned rtflags;       /*!< Runtime flags. \note For internal use. */
	struct chx_value value; /*!< (Back)quoted/comma'd/spliced value. */
};

/*! \brief Creates a \ref chx_value of type \ref CHEAX_ID.
 *
 * \param id Identifier value for the expression.
 *
 * \sa chx_id, CHEAX_ID, cheax_id_value_proc()
 */
CHX_API struct chx_value cheax_id(CHEAX *c, const char *id);

/*! \brief Turns \ref chx_id into \ref chx_value.
 * \sa cheax_id_value_proc()
 */
#define cheax_id_value(X) ((struct chx_value){ .type = CHEAX_ID, .data.as_id = (X) })

/*! \brief Turns \ref chx_id into \ref chx_value.
 * Like cheax_id_value(), but a function and not a macro.
 */
CHX_API struct chx_value cheax_id_value_proc(struct chx_id *id);

/*! \brief Creates a \ref chx_value of type \ref CHEAX_INT.
 *
 * \param value Integral value for the object.
 *
 * \sa chx_int, CHEAX_INT, cheax_int_proc()
 */
#define cheax_int(X) ((struct chx_value){ .type = CHEAX_INT, .data.as_int = (X) })

/*! \brief Creates a \ref chx_value of type \ref CHEAX_INT.
 * Like cheax_int(), but a function and not a macro.
 */
CHX_API struct chx_value cheax_int_proc(chx_int value);

/*! \brief Creates \ref chx_value `true`.
 * \sa cheax_false(), cheax_bool(), chx_int, CHEAX_BOOL
 */
#define cheax_true() ((struct chx_value){ .type = CHEAX_BOOL, .data.as_int = 1 })

/*! \brief Creates \ref chx_value `false`.
 * \sa cheax_true(), cheax_bool(), chx_int, CHEAX_BOOL
 */
#define cheax_false() ((struct chx_value){ .type = CHEAX_BOOL, .data.as_int = 0 })

/*! \brief Creates \ref chx_value of type \ref CHEAX_BOOL.
 *
 * \param value Boolean value for the object.
 *
 * \sa cheax_true(), cheax_false(), chx_int, CHEAX_BOOL
 */
#define cheax_bool(X) ((struct chx_value){ .type = CHEAX_BOOL, .data.as_int = (X) ? 1 : 0 })

/*! \brief Creates a \ref chx_value of type \ref CHEAX_BOOL.
 * Like cheax_bool(), but a function and not a macro.
 */
CHX_API struct chx_value cheax_bool_proc(bool value);

/*! \brief Creates a \ref chx_value of type \ref CHEAX_DOUBLE.
 *
 * \param value Floating point value for the object.
 *
 * \sa chx_double, CHEAX_DOUBLE
 */
#define cheax_double(X) ((struct chx_value){ .type = CHEAX_DOUBLE, .data.as_double = (X) })

/*! \brief Creates a \ref chx_value of type \ref CHEAX_DOUBLE.
 * Like cheax_double(), but a function and not a macro.
 */
CHX_API struct chx_value cheax_double_proc(chx_double value);

/*! \brief List type.
 *
 * Also known as a cons cell, an S-expression or a singly-linked list.
 *
 * \sa CHEAX_LIST, cheax_list(), chx_value
 */
struct chx_list {
	unsigned rtflags;       /*!< Runtime flags. \note For internal use. */
	struct chx_value value; /*!< The value of the s-expression node. */
	struct chx_list *next;  /*!< The next node in the s-expression. */
};

/*! \brief Creates a list.
 *
 * \param car Node value.
 * \param cdr Next node or `NULL`.
 *
 * \sa chx_list, CHEAX_LIST
 */
CHX_API struct chx_value cheax_list(CHEAX *c, struct chx_value car, struct chx_list *cdr);

/*! \brief Turns \ref chx_list into \ref chx_value.
 * \sa cheax_list_value_proc()
 */
#define cheax_list_value(X) ((struct chx_value){ .type = CHEAX_LIST, .data.as_list = (X) })

/*! \brief Turns \ref chx_list into \ref chx_value.
 * Like \ref cheax_list_value(), but a function and not a macro.
 */
CHX_API struct chx_value cheax_list_value_proc(struct chx_list *list);

/*! \brief Function or macro type.
 *
 * Functions are created with the `(fn)` built-in, and cannot be
 * constructed through the C API. Macros are created through the
 * `(defmacro)` built-in, and live only inside a special-purpose
 * environment.
 *
 * Functions and macros cannot be constructed through the C API.
 *
 * \sa CHEAX_FUNC, cheax_apply(), cheax_macroexpand(), cheax_macroexpand_once()
 */
struct chx_func {
	unsigned rtflags;
	struct chx_value args;       /*!< Lambda argument list expression. */
	struct chx_list *body;       /*!< Lambda body. */
	struct chx_env *lexenv;      /*!< Lexical environment. \note Internal use only. */
};

#define cheax_func_value(X) ((struct chx_value){ .type = CHEAX_FUNC, .data.as_func = (X) })
CHX_API struct chx_value cheax_func_value_proc(struct chx_func *fn);

/*! \brief Type for C functions to be invoked from cheax.
 *
 * \param args The argument list as the function was invoked. The
 *             arguments are given as is, not pre-evaluated. E.g. if
 *             cheax passes an identifier to the function as an
 *             argument, it will apear as an identifier in the argument
 *             list, not as the value of the symbol it may represent.
 * \param info User-provided data.
 *
 * \returns The function's return value to be delivered back to cheax.
 *
 * \sa chx_ext_form, cheax_defsyntax(), cheax_ext_func(), cheax_defun()
 */
typedef struct chx_value (*chx_func_ptr)(CHEAX *c, struct chx_list *args, void *info);

enum {
	CHEAX_VALUE_OUT, CHEAX_TAIL_OUT,
};

union chx_eval_out {
	struct {
		struct chx_value tail;
		struct chx_env *pop_stop;
	} ts;

	struct chx_value value;
};

typedef int (*chx_tail_func_ptr)(CHEAX *c,
                                 struct chx_list *args,
                                 void *info,
                                 struct chx_env *pop_stop,
                                 union chx_eval_out *out);

/*! \brief Cheax external/user function expression.
 * \sa cheax_defun(), CHEAX_EXT_FUNC, chx_func_ptr
 */
struct chx_ext_func {
	unsigned rtflags;
	const char *name;     /*!< The function's name, used by cheax_print(). */
	chx_func_ptr perform;
	void *info;           /*!< Callback info to be passed upon invocation. */
};

/*! \brief Creates a cheax external/user function expression.
 *
 * External functions, unlike special operators, have their arguments
 * pre-evaluated.
 *
 * \param perform Function pointer to be invoked.
 * \param name    Function name as will be used by cheax_print().
 * \param info    Callback info to be passed upon invocation.
 */
CHX_API struct chx_value cheax_ext_func(CHEAX *c,
                                        const char *name,
                                        chx_func_ptr perform,
                                        void *info);

#define cheax_ext_func_value(X) ((struct chx_value){ .type = CHEAX_EXT_FUNC, .data.as_ext_func = (X) })
CHX_API struct chx_value cheax_ext_func_value_proc(struct chx_ext_func *sf);

/*! \brief Creates a quoted cheax expression.
 *
 * \param value Expression to be quoted.
 *
 * \sa chx_quote, CHEAX_QUOTE
 */
CHX_API struct chx_value cheax_quote(CHEAX *c, struct chx_value value);

#define cheax_quote_value(X) ((struct chx_value){ .type = CHEAX_QUOTE, .data.as_quote = (X) })
CHX_API struct chx_value cheax_quote_value_proc(struct chx_quote *quote);

/*! \brief Creates a backquoted cheax expression.
 *
 * \param value Expression to be backquoted.
 *
 * \sa chx_quote, CHEAX_BACKQUOTE
 */
CHX_API struct chx_value cheax_backquote(CHEAX *c, struct chx_value value);

#define cheax_backquote_value(X) ((struct chx_value){ .type = CHEAX_BACKQUOTE, .data.as_quote = (X) })
CHX_API struct chx_value cheax_backquote_value_proc(struct chx_quote *bkquote);

/*! \brief Creates a cheax comma expression.
 *
 * \param value Expression following comma.
 *
 * \sa chx_quote, CHEAX_COMMA
 */
CHX_API struct chx_value cheax_comma(CHEAX *c, struct chx_value value);

#define cheax_comma_value(X) ((struct chx_value){ .type = CHEAX_COMMA, .data.as_quote = (X) })
CHX_API struct chx_value cheax_comma_value_proc(struct chx_quote *comma);

/*! \brief Creates a cheax comma splice expression.
 *
 * \param value Expression following comma splice.
 *
 * \sa chx_quote, CHEAX_SPLICE
 */
CHX_API struct chx_value cheax_splice(CHEAX *c, struct chx_value value);

#define cheax_splice_value(X) ((struct chx_value){ .type = CHEAX_SPLICE, .data.as_quote = (X) })
CHX_API struct chx_value cheax_splice_value_proc(struct chx_quote *splice);

/*! \brief Cheax string expression.
 * \sa cheax_string(), cheax_nstring(), CHEAX_STRING, cheax_strlen(),
 *     cheax_substr(), cheax_strdup()
 */
struct chx_string;

/*! \brief Size of string in number of bytes.
 *
 * \param str String.
 *
 * \returns Size of given string, or zero if \a str is `NULL`.
 */
CHX_API size_t cheax_strlen(CHEAX *c, struct chx_string *str);

/*! \brief Creates a cheax string expression.
 *
 * \param value Null-terminated value for the string.
 *
 * \sa chx_string, cheax_nstring(), CHEAX_STRING
 */
CHX_API struct chx_value cheax_string(CHEAX *c, const char *value);

/*! \brief Creates a cheax string expression of given length.
 *
 * \param value Value for the string.
 * \param len   Length of the string.
 *
 * \sa chx_string, cheax_string(), CHEAX_STRING
 */
CHX_API struct chx_value cheax_nstring(CHEAX *c, const char *value, size_t len);

#define cheax_string_value(X) ((struct chx_value){ .type = CHEAX_STRING, .data.as_string = (X) })
CHX_API struct chx_value cheax_string_value_proc(struct chx_string *string);

/*! \brief Takes substring of given cheax string.
 *
 * Sets cheax_errno() to \ref CHEAX_EINDEX if substring is out of bounds.
 *
 * \param str Initial string.
 * \param pos Substring starting offset in number of bytes.
 * \param len Substring length in number of bytes.
 */
CHX_API struct chx_value cheax_substr(CHEAX *c, struct chx_string *str, size_t pos, size_t len);

/*! \brief Allocates a null-terminated copy of given chx_string.
 *
 * Make sure to free() result after use.
 *
 * \param str String.
 *
 * \returns Null terminated string or `NULL` if \a str is `NULL`.
 */
CHX_API char *cheax_strdup(struct chx_string *str);

/*! \brief Creates a cheax user pointer expression.
 *
 * \param value Pointer value for the expression.
 * \param type  Type alias for the expression. Must not be a basic type,
 *              and must resolve to \ref CHEAX_USER_PTR.
 *
 * \sa chx_user_ptr, CHEAX_USER_PTR
 */
CHX_API struct chx_value cheax_user_ptr(CHEAX *c, void *value, int type);

/*! \brief Cheax environment, storing symbols and their values.
 *
 * \sa cheax_push_env(), cheax_enter_env(), cheax_pop_env(),
 *     CHEAX_ENV, cheax_env()
 */
struct chx_env;

/*! \brief Currently active \ref chx_env.
 *
 * \sa cheax_push_env(), cheax_enter_env(), cheax_pop_env()
 *
 * \returns Currently active \ref chx_env, or `NULL` if currently
 *          running in the global scope.
 */
CHX_API struct chx_value cheax_env(CHEAX *c);

#define cheax_env_value(X) ((struct chx_value){ .type = CHEAX_ENV, .data.as_env = (X) })
CHX_API struct chx_value cheax_env_value_proc(struct chx_env *env);

#if __STDC_VERSION__ + 0 >= 201112L
#define cheax_value(v)                                              \
	(_Generic((0,v),                                            \
		int:                 cheax_int_proc,                \
		chx_int:             cheax_int_proc,                \
		double:              cheax_double_proc,             \
		float:               cheax_double_proc,             \
		struct chx_env *:    cheax_env_value_proc,          \
		struct chx_id *:     cheax_id_value_proc,           \
		struct chx_string *: cheax_string_value_proc,       \
		struct chx_list *:   cheax_list_value_proc)(v))
#endif

struct chx_sym;

typedef struct chx_value (*chx_getter)(CHEAX *c, struct chx_sym *sym);
typedef void (*chx_setter)(CHEAX *c, struct chx_sym *sym, struct chx_value value);
typedef void (*chx_finalizer)(CHEAX *c, struct chx_sym *sym);

/*! \brief Custom symbol. */
struct chx_sym {
	void *user_info;          /*!< User-provided data to be passed along to
				       \a get, \a set and \a fin */
	chx_getter get;           /*!< Getter. */
	chx_setter set;           /*!< Setter. */
	chx_finalizer fin;        /*!< Finalizer. */
	struct chx_value protect; /*!< Value that remains protected from garbage
	                               collection as long as the symbol exists. */
};

typedef int chx_ref;

/*! \brief Increase reference count on cheax value, preventing it from
 *         gc deletion when cheax_eval() is called.
 * \sa cheax_unref()
 */
CHX_API chx_ref cheax_ref(CHEAX *c, struct chx_value value);
CHX_API chx_ref cheax_ref_ptr(CHEAX *c, void *obj);

/*! \brief Decrease reference count on cheax value, potentially allowing
 *         it to be deleted by gc when cheax_eval() is called.
 * \sa cheax_ref()
 */
CHX_API void cheax_unref(CHEAX *c, struct chx_value value, chx_ref ref);
CHX_API void cheax_unref_ptr(CHEAX *c, void *obj, chx_ref ref);

/*! \brief Creates a new type code as an alias for another.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if
 * \li \a name is `NULL`;
 * \li \a base_type is not a valid type code; or
 * \li \a name already names a type.
 *
 * \param name      Name for the new type code in the cheax environment.
 * \param base_type Base type code to create an alias for.
 *
 * \returns The new type code. -1 if unsuccessful.
 */
CHX_API int cheax_new_type(CHEAX *c, const char *name, int base_type);

/*! \brief Looks up the type code of a named type.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a name is NULL.
 *
 * \param name Type name.
 *
 * \returns The type code of a type with the given name, or -1 if
 *          unsuccessful.
 */
CHX_API int cheax_find_type(CHEAX *c, const char *name);

/*! \brief Checks whether a given type code is valid.
 *
 * \param type Code to check.
 *
 * \returns Whether \a type is a valid type code.
 */
CHX_API bool cheax_is_valid_type(CHEAX *c, int type);

/*! \brief Checks whether a given type code is a basic type.
 *
 * \param type Type code to check.
 *
 * \returns Whether \a type is a basic type.
 */
CHX_API bool cheax_is_basic_type(CHEAX *c, int type);

/*! \brief Checks whether a given type code is a user-defined type code.
 *
 * \param type Type code to check.
 *
 * \returns Whether \a type is a user-defined type code.
 */
CHX_API bool cheax_is_user_type(CHEAX *c, int type);

/*! \brief Gets the base type for a given type.
 *
 * The base type of a given type is either the type itself for basic
 * types, or the type for which a user-defined type is an alias. Note
 * that this operation is only applied once, and hence the return value
 * is not necessarily a basic type.
 *
 * Sets cheax_errno() to \ref CHEAX_EEVAL if \a type could not be
 * resolved.
 *
 * \param type Type code to get the base type of.
 *
 * \returns The base type of \a type, or -1 if unsuccessful.
 *
 * \sa cheax_resolve_type()
 */
CHX_API int cheax_get_base_type(CHEAX *c, int type);

/*! \brief Resolves the basic type to which a given type code refers.
 *
 * Progressively applies cheax_get_base_type() until a basic type is
 * reached.
 *
 * Sets cheax_errno() to \ref CHEAX_EEVAL if \a type could not be
 * resolved.
 *
 * \param type Type code to resolve.
 *
 * \returns The basic type to which \a type refers, or -1 if unsuccessful.
 *
 * \sa cheax_get_base_type()
 */
CHX_API int cheax_resolve_type(CHEAX *c, int type);

/*! @} */

/*!
 * \defgroup ErrorHandling Error handling
 * \brief Error codes and ways to deal with them.
 * @{
 */

/*! \brief Pre-defined cheax error codes.
 * \sa cheax_errno(), cheax_throw(), cheax_new_error_code()
 */
enum {
	CHEAX_ENOERR     = 0x0000, /*!< No error. Equal to 0. \sa cheax_clear_errno() */

	/* Read errors */
	CHEAX_EREAD      = 0x0001, /*!< Generic read error. */
	CHEAX_EEOF       = 0x0002, /*!< Unexpected end-of-file. */

	/* Eval/runtime errors */
	CHEAX_EEVAL      = 0x0101, /*!< Generic eval error */
	CHEAX_ENOSYM     = 0x0102, /*!< Symbol not found error. */
	CHEAX_ESTACK     = 0x0103, /*!< Stack overflow error. */
	CHEAX_ETYPE      = 0x0104, /*!< Invalid type error. */
	CHEAX_EMATCH     = 0x0105, /*!< Unable to match expression error. */
	CHEAX_ESTATIC    = 0x0106, /*!< Preprocessing failed. */
	CHEAX_EDIVZERO   = 0x0107, /*!< Division by zero error. */
	CHEAX_EREADONLY  = 0x0108, /*!< Attempted write to read-only symbol error. */
	CHEAX_EWRITEONLY = 0x0109, /*!< Attempted read from write-only symbol error. */
	CHEAX_EEXIST     = 0x010A, /*!< Symbol already exists error. */
	CHEAX_EVALUE     = 0x010B, /*!< Invalid value error. */
	CHEAX_EOVERFLOW  = 0x010C, /*!< Integer overflow error. */
	CHEAX_EINDEX     = 0x010D, /*!< Invalid index error. */
	CHEAX_EIO        = 0x010E, /*!< IO error. */

	CHEAX_EAPI       = 0x0200, /*!< API error. \note Not to be thrown from within cheax code. */
	CHEAX_ENOMEM     = 0x0201, /*!< Out-of-memory error. \note Not to be thrown from within cheax code. */

	CHEAX_EUSER0     = 0x0400, /*!< First user-defineable error code. \sa cheax_new_error_code() */
};

#define ERR_NAME_PAIR(NAME) {#NAME, CHEAX_##NAME}

#define CHEAX_BUILTIN_ERROR_NAMES(var)                        \
static const struct { const char *name; int code; } var[] = { \
	ERR_NAME_PAIR(ENOERR),                                \
	                                                      \
	ERR_NAME_PAIR(EREAD), ERR_NAME_PAIR(EEOF),            \
	                                                      \
	ERR_NAME_PAIR(EEVAL), ERR_NAME_PAIR(ENOSYM),          \
	ERR_NAME_PAIR(ESTACK), ERR_NAME_PAIR(ETYPE),          \
	ERR_NAME_PAIR(EMATCH), ERR_NAME_PAIR(ESTATIC),        \
	ERR_NAME_PAIR(EDIVZERO), ERR_NAME_PAIR(EREADONLY),    \
	ERR_NAME_PAIR(EWRITEONLY), ERR_NAME_PAIR(EEXIST),     \
	ERR_NAME_PAIR(EVALUE), ERR_NAME_PAIR(EOVERFLOW),      \
	ERR_NAME_PAIR(EINDEX), ERR_NAME_PAIR(EIO),            \
	                                                      \
	ERR_NAME_PAIR(EAPI), ERR_NAME_PAIR(ENOMEM)            \
}

/*! \brief Gets the value of the current cheax error code.
 *
 *
 * \sa cheax_throw(), cheax_new_error_code(), cheax_ft()
 */
CHX_API int cheax_errno(CHEAX *c);

/*! \brief Macro to fall through to a pad in case of an error.
 *
 * Jumps to \a pad if cheax_errno() is not 0. Most commonly used after
 * cheax_eval().
 *
 * \param pad Label to jump to in case of an error.
 */
#define cheax_ft(c, pad) { if (cheax_errno(c) != 0) goto pad; }

/*! \brief Prints the current cheax error code and error message.
 *
 * \param s Argument string, will be printed followed by a colon and an
 *          error description.
 */
CHX_API void cheax_perror(CHEAX *c, const char *s);

/*! \brief Sets cheax_errno() to 0.
 *
 *
 * \sa cheax_errno()
 */
CHX_API void cheax_clear_errno(CHEAX *c);

/*! \brief Sets cheax_errno() to the given value.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a code is 0.
 *
 * \param code Error code.
 * \param msg  Error message string or `NULL`.
 *
 * \sa cheax_errno(), cheax_clear_errno()
 */
CHX_API void cheax_throw(CHEAX *c, int code, struct chx_string *msg);
CHX_API void cheax_throwf(CHEAX *c, int code, const char *fmt, ...);
CHX_API void cheax_add_bt(CHEAX *c);

/*! \brief Creates a new error code with a given name.
 *
 * Such new error codes can then be thrown with cheax_throw() or from
 * within cheax.
 *
 * Does not change cheax_errno(), except when \a name is NULL, in which
 * case it is set to \ref CHEAX_EAPI.
 *
 * \param name The error code's name. Used to make the error code
 *             available in cheax, and for error reporting by
 *             cheax_perror().
 *
 * \returns The newly created error code value.
 */
CHX_API int cheax_new_error_code(CHEAX *c, const char *name);

/*! \brief Looks up the value of a named error code.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a name is NULL.
 *
 * \param name Error code name.
 *
 * \returns The error code carrying the given name, or -1 if
 *          unsuccessful.
 */
CHX_API int cheax_find_error_code(CHEAX *c, const char *name);

/*! @} */

/*!
 * \defgroup SetupConfig Setup and configuration
 * \brief Functions and datastructures to initialize, clean up and
 *        configure a cheax virtual machine instance.
 * @{
 */

/*! \brief Initializes a new cheax virtual machine instance.
 * \sa cheax_load_features(), cheax_load_prelude(), cheax_destroy(),
 *     cheax_version()
 */
CHX_API CHEAX *cheax_init(void);

/*! \brief Returns cheax library version as a string in the static
 *         storage class.
 */
CHX_API const char *cheax_version(void);

/*! \brief Loads extra functions or language features into the cheax
 *         environment, including 'unsafe' ones.
 *
 * Supported values for \a feat are:
 * \li `"file-io"` to load `fopen` and `fclose` built-ins;
 * \li `"set-max-stack-depth"` to load the <tt>set-max-stack-depth</tt> built-in;
 * \li `"gc"` to load the `gc` built-in function;
 * \li `"exit"` to load the `exit` function;
 * \li `"stdin"` to expose the `stdin` variable;
 * \li `"stdout"` to expose the `stdout` variable;
 * \li `"stderr"` to expose the `stderr` variable;
 * \li `"stdio"` to expose `stdin`, `stdout` and `stderr`;
 * \li `"all"` to load every feature available (think twice before using).
 *
 * A feature can only be loaded once. Attempting to load a feature more
 * than once will cause no action.
 *
 * \param feat Which feature to load.
 *
 * \returns 0 if the given feature was loaded successfully, or -1 if the
 *          given feature is not supported.
 */
CHX_API int cheax_load_feature(CHEAX *c, const char *feat);

/*! \brief Loads the cheax standard library.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if the standard library
 * could not be found.
 *
 *
 * \returns 0 if everything succeeded without errors, -1 if there was an
 *          error finding or loading the standard library.
 */
CHX_API int cheax_load_prelude(CHEAX *c);

/*! \brief Destroys a cheax virtual machine instance, freeing its
 *         resources.
 *
 * Any use of \a c after calling cheax_destroy() on it results in
 * undefined behavior.
 *
 */
CHX_API void cheax_destroy(CHEAX *c);

/*! \brief Get value of integer configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if no option of integer type
 * with name \a opt exists.
 *
 * \param opt Option name.
 *
 * \returns Option value, or 0 upon failure.
 */
CHX_API int cheax_config_get_int(CHEAX *c, const char *opt);

/*! \brief Set value of integer configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if option \a opt does not have
 * integer type, or if \a value is otherwise invalid for option \a opt.
 * Fails silently in case no option with name \a opt could be found.
 *
 * \param opt   Option name.
 * \param value Option value.
 *
 * \returns 0 if an integer option with name \a opt was found, -1 otherwise.
 */
CHX_API int cheax_config_int(CHEAX *c, const char *opt, int value);

/*! \brief Get value of boolean configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if no option of boolean type
 * with name \a opt exists.
 *
 * \param opt Option name.
 *
 * \returns Option value, or \c false upon failure.
 */
CHX_API bool cheax_config_get_bool(CHEAX *c, const char *opt);

/*! \brief Set value of boolean configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if option \a opt does not have
 * boolean type, or if \a value is otherwise invalid for option \a opt.
 * Fails silently in case no option with name \a opt could be found.
 *
 * \param opt   Option name.
 * \param value Option value.
 *
 * \returns 0 if a boolean option with name \a opt was found, -1 otherwise.
 */
CHX_API int cheax_config_bool(CHEAX *c, const char *opt, bool value);

/*! \brief Information about cheax config option. */
struct chx_config_help {
	const char *name;    /*!< Option name. */
	int type;            /*!< Option type. */
	const char *metavar; /*!< Printable argument name. */
	const char *help;    /*!< Help text. */
};

/*! \brief Load information about all cheax config options.
 *
 * \param help     Output parameter. Make sure to free() after use.
 * \param num_opts Output parameter, will point to length of output
 *                 array.
 *
 * \returns 0 if everything succeeded without errors, -1 otherwise.
 */
CHX_API int cheax_config_help(struct chx_config_help **help, size_t *num_opts);

/*! @} */

/*! \brief Converts \ref chx_list to array.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a array_ptr or \a length
 * is NULL, or to \ref CHEAX_ENOMEM if array allocation failed.
 *
 * \param list      List to convert.
 * \param array_ptr Output parameter, will point to array of cheax_value
 *                  pointers. Make sure to cheax_free() after use.
 * \param length    Output parameter, will point to length of output
 *                  array.
 *
 * \returns 0 if everything succeeded without errors, -1 if there was an
 *          error (most likely \ref CHEAX_ENOMEM).
 *
 * \sa cheax_array_to_list()
 */
CHX_API int cheax_list_to_array(CHEAX *c,
                                struct chx_list *list,
                                struct chx_value **array_ptr,
                                size_t *length);

/*! \brief Converts array to \ref chx_list.
 *
 * Throws \ref CHEAX_EAPI if \a array is `NULL`.
 *
 * \param array  Array of \ref chx_value.
 * \param length Length of \a array.
 *
 * \sa cheax_list_to_array()
 */
CHX_API struct chx_value cheax_array_to_list(CHEAX *c,
                                             struct chx_value *array,
                                             size_t length);

/*! \brief Options for symbol declaration and value matching.
 *
 * \sa cheax_def(), cheax_match(), cheax_sync_int(), cheax_sync_bool(),
 *     cheax_sync_float(), cheax_sync_double()
 */
enum {
	CHEAX_SYNCED     = 0x01, /*!< Reserved. \note Unused. */
	CHEAX_READONLY   = 0x02, /*!< Marks a symbol read-only. */
	CHEAX_WRITEONLY  = 0x04, /*!< Marks a symbol write-only. */

	/*! \brief For cheax_match() and cheax_match_in(): evaluate list
	 *         nodes before matching them. */
	CHEAX_EVAL_NODES = 0x08,
};

/*! \brief Pushes new empty environment to environment stack.
 *
 */
CHX_API void cheax_push_env(CHEAX *c);

/*! \brief Pushes new bifurcated environment to environment stack.
 *
 * Lookups will first look in \a main, then look further down the stack.
 * New symbols are declared only in \a main.
 *
 * \param main  Main branch of bifurcated environment.
 */
CHX_API void cheax_enter_env(CHEAX *c, struct chx_env *main);

/*! \brief Pops environment off environment stack.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if environment stack is empty.
 *
 */
CHX_API void cheax_pop_env(CHEAX *c);

/*! \brief Define symbol with custom getter and setter.
 *
 * Throws
 * \li \ref CHEAX_EAPI if \a id is `NULL`, or \a get and \a set are
 *      both `NULL`; or
 * \li \ref CHEAX_EEXIST if a symbol with name \a id already exists.
 *
 * \param id        Identifier for new symbol.
 * \param get       Getter for symbol.
 * \param set       Setter for symbol.
 * \param fin       Finalizer for symbol.
 * \param user_info User data to be passed to getter, setter and finalizer.
 *
 * \returns The newly created symbol, or `NULL` if unsuccessful.
 *
 * \sa chx_sym, cheax_def()
 */
CHX_API struct chx_sym *cheax_defsym(CHEAX *c, const char *id,
                                     chx_getter get, chx_setter set,
                                     chx_finalizer fin, void *user_info);

/*! \brief Creates a new symbol in the cheax environment.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a id is `NULL`, or to
 * \ref CHEAX_EEXIST if a symbol with name \a id already exists.
 *
 * \param id     Variable identifier.
 * \param value  Initial value or the symbol. May be `NULL`.
 * \param flags  Variable flags. Use 0 if there are no special needs.
 *
 * \sa cheax_get(), cheax_set()
 */
CHX_API void cheax_def(CHEAX *c, const char *id, struct chx_value value, int flags);

/*! \brief Obtains the value of the given symbol.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a id is `NULL`, or to
 * \ref CHEAX_ENOSYM if no symbol with name \a id could be found.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param id Identifier to look up.
 *
 * \returns The value of the given symbol. Always `NULL` in case of an
 *          error.
 *
 * \sa cheax_get_from(), cheax_set()
 */
CHX_API struct chx_value cheax_get(CHEAX *c, const char *id);

/*! Attempts to get the value of a given symbol.
 *
 * Like cheax_get(), but does not throw \ref CHEAX_ENOSYM if no symbol
 * was found.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param id  Identifier to look up.
 * \param out Output value. Not written to if no symbol was found.
 *
 * \returns `true` if symbol was found, `false` otherwise.
 *
 * \sa cheax_get()
 */
CHX_API bool cheax_try_get(CHEAX *c, const char *id, struct chx_value *out);

/*! \brief Retrieves the value of the given symbol, performing symbol
 *         lookup only in the specified environment.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a id is `NULL`, or to
 * \ref CHEAX_ENOSYM if no symbol with name \a id could be found.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param env Environment to look up identifier in.
 * \param id  Identifier to look up.
 *
 * \returns The value of the given symbol. Always `NULL` in case of an
 *          error.
 *
 * \sa cheax_get(), cheax_try_get_from()
 */
CHX_API struct chx_value cheax_get_from(CHEAX *c, struct chx_env *env, const char *id);

/*! Attempts to get the value of a given symbol, performing symbol
 *           lookup only in the specified environment.
 *
 * Like cheax_get_from(), but does not throw \ref CHEAX_ENOSYM if no
 * symbol was found.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param env Environment to look up identifier in.
 * \param id  Identifier to look up.
 * \param out Output value. Not written to if no symbol was found.
 *
 * \returns `true` if symbol was found, `false` otherwise.
 *
 * \sa cheax_get_from()
 */
CHX_API bool cheax_try_get_from(CHEAX *c,
                                struct chx_env *env,
                                const char *id,
                                struct chx_value *out);

/*! \brief Sets the value of a symbol.
 *
 * Sets cheax_errno() to
 * \li \ref CHEAX_EAPI if \a id is `NULL`;
 * \li \ref CHEAX_ENOSYM if no symol with name \a id could be found;
 * \li \ref CHEAX_EREADONLY if the given symbol was declared read-only; or
 * \li \ref CHEAX_ETYPE if symbol is synchronised and type of
 *     \a value is invalid.
 *
 * cheax_set() cannot be used to declare a new symbol, use
 * cheax_def() instead.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param id    Identifier of the symbol to look up and set.
 * \param value New value for the symbol with the given identifier.
 *
 * \sa cheax_get(), cheax_def()
 */
CHX_API void cheax_set(CHEAX *c, const char *id, struct chx_value value);

/*! \brief Shorthand function to declare an external function the cheax
 *         environment.
 *
 * Shorthand for:
\code{.c}
cheax_def(c, id, &cheax_ext_func(c, id, perform, info)->base, CHEAX_READONLY);
\endcode
 *
 * \param id      Identifier for the external functions.
 * \param perform Callback for the new external functions.
 * \param info    Callback info for the new external functions.
 *
 * \sa chx_ext_func, cheax_ext_func(), cheax_def(), cheax_defsyntax()
 */
CHX_API void cheax_defun(CHEAX *c, const char *id, chx_func_ptr perform, void *info);
CHX_API void cheax_defsyntax(CHEAX *c,
                             const char *id,
                             chx_tail_func_ptr perform,
                             chx_func_ptr preproc,
                             void *info);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_int(CHEAX *c, const char *name, int *var, int flags);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_bool(CHEAX *c, const char *name, bool *var, int flags);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_float(CHEAX *c, const char *name, float *var, int flags);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_double(CHEAX *c, const char *name, double *var, int flags);

/*! \brief Synchronizes a null-terminated string buffer from C with a
 *         symbol in the cheax environment.
 *
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   String buffer to synchronize.
 * \param size  Capacity of buffer, including null byte.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_nstring(CHEAX *c, const char *name, char *buf, size_t size, int flags);

/*! \brief Matches a cheax expression to a given pattern.
 *
 * May declare some symbols in currently active environment, regardless
 * of return value, and does \em not set cheax_errno() to
 * \ref CHEAX_EMATCH if the expression did not match the pattern.
 * If \a flags contains \ref CHEAX_EVAL_NODES, evaluation of node values
 * occurs in environment \a env.
 *
 * \param env   Environment to evaluate nodes in.
 * \param pan   Pattern to match against.
 * \param match Expression to match against \a pan.
 * \param flags Variable flags for newly defined matched symbols. Use 0
 *              if there are no special needs, although you should
 *              probably use \ref CHEAX_READONLY.
 *
 * \returns Whether \a match matched the pattern \a pan.
 */
CHX_API bool cheax_match_in(CHEAX *c,
                            struct chx_env *env,
                            struct chx_value pan,
                            struct chx_value match,
                            int flags);

/*! \brief Matches a cheax expression to a given pattern.
 *
 * May declare some symbols in currently active environment, regardless
 * of return value, and does \em not set cheax_errno() to
 * \ref CHEAX_EMATCH if the expression did not match the pattern.
 *
 * \param pan   Pattern to match against.
 * \param match Expression to match against \a pan.
 * \param flags Variable flags for newly defined matched symbols. Use 0
 *              if there are no special needs, although you should
 *              probably use \ref CHEAX_READONLY.
 *
 * \returns Whether \a match matched the pattern \a pan.
 */
CHX_API bool cheax_match(CHEAX *c, struct chx_value pan, struct chx_value match, int flags);

/*! \brief Tests whether two given cheax expressions are equal.
 *
 * \param l Left.
 * \param r Right.
 *
 * \returns Whether the given cheax expressions are equal in value.
 */
CHX_API bool cheax_eq(CHEAX *c, struct chx_value l, struct chx_value r);
CHX_API bool cheax_equiv(struct chx_value l, struct chx_value r);

/*! \brief Attempts to cast an expression to a given type.
 *
 * Sets cheax_errno() to \ref CHEAX_ETYPE if casting is not possible.
 *
 * \param v    Expression to cast.
 * \param type Code of the type to cast to.
 *
 * \returns The cast expression. Always `NULL` if unsuccessful.
 */
CHX_API struct chx_value cheax_cast(CHEAX *c, struct chx_value v, int type);

/*! \brief Reads value from file.
 *
 * Core element of the read, eval, print loop.
 *
 * Throws
 * \li \ref CHEAX_EAPI if \a f is `NULL`;
 * \li \ref CHEAX_EREAD upon syntax error; or
 * \li \ref CHEAX_EEOF if (and only if) end-of-file was encountered
 *      before an S-expression was closed.
 *
 * \param f File handle to read from.
 *
 * \returns The value that was read from the file. If no value was read
 *          and end-of-file was reached, the function will return `nil`
 *          and no error will be thrown. Check `feof(f)` to test for
 *          this condition.
 *
 * \sa cheax_read_at(), cheax_readstr(), cheax_eval(), cheax_print()
 */
CHX_API struct chx_value cheax_read(CHEAX *c, FILE *f);

/*! \brief Reads value from file and reports back line and column
 *         information.
 *
 * Like cheax_read(), but with additional parameters used for
 * error reporting and diagnostics.
 *
 * \param f    File handle to read from.
 * \param path Path of input file. Used for error reporting and debug
 *             info generation.
 * \param line Pointer to current line number. Will be updated as
 *             newlines are read. Will be ignored if `NULL`.
 * \param pos  Pointer to current column number. Will be updated as
 *             input is read. Will be ignored if `NULL`.
 *
 * \sa cheax_read(), cheax_readstr_at()
 */
CHX_API struct chx_value cheax_read_at(CHEAX *c,
                                       FILE *f,
                                       const char *path,
                                       int *line,
                                       int *pos);

/*! \brief Reads value from string.
 *
 * Like cheax_read(), but reading directly from a string, rather than
 * from a file.
 *
 * Throws
 * \li \ref CHEAX_EAPI if \a str is `NULL`;
 * \li \ref CHEAX_EREAD upon syntax error; or
 * \li \ref CHEAX_EEOF if (and only if) end-of-file was encountered
 *      before an S-expression was closed.
 *
 * \param str Null-terminated string to read from.
 *
 * \returns The value that was read from the string. If no value was read
 *          and the null terminator was reached, the function will
 *          return `nil` and no error will be thrown. Unlike with
 *          cheax_read() and cheax_readstr_at(), the API does not
 *          distinguishes this condition from a successful reading of
 *          the value `nil` from the string.
 *
 * \sa cheax_read_at(), cheax_readstr(), cheax_eval(), cheax_print()
 */
CHX_API struct chx_value cheax_readstr(CHEAX *c, const char *str);

/*! \brief Reads value from string, updates the string to reference
 *         the byte where it left off reading, and reports back line and
 *         column information.
 *
 * Like cheax_readstr(), but with additional parameters used for
 * error reporting and diagnostics. Additionally, the string pointer is
 * updated, allowing one to call the function multiple times in
 * succession to read multiple values.
 *
 * Throws \ref CHEAX_EREAD and \ref CHEAX_EEOF as would cheax_readstr(),
 * and throws \ref CHEAX_EAPI if \a str points to, or is itself, `NULL`.
 *
 * \param str  Pointer to string to read from. Will, if no errors were
 *             thrown, point to the first byte in the string not
 *             examined by the parser.
 * \param path "Path" of input string. Used for error reporting and
 *             debug info generation.
 * \param line Pointer to current line number. Will be updated as
 *             newlines are read. Will be ignored if `NULL`.
 * \param pos  Pointer to current column number. Will be updated as
 *             input is read. Will be ignored if `NULL`.
 *
 * \sa cheax_readstr(), cheax_read_at()
 */
CHX_API struct chx_value cheax_readstr_at(CHEAX *c,
                                          const char **str,
                                          const char *path,
                                          int *line,
                                          int *pos);

/*! \brief Expand given expression until it is no longer a macro form.
 *
 * Macros are defined using the `(defmacro)` built-in. Throws
 * \ref CHEAX_ESTATIC in case of an internal error.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param expr Expression in which to expand macro forms.
 *
 * \sa cheax_macroexpand_once()
 */
CHX_API struct chx_value cheax_macroexpand(CHEAX *c, struct chx_value expr);

/*! \brief Expand expression if it is a macro form.
 *
 * Macros are defined using the `(defmacro)` built-in. Throws
 * \ref CHEAX_ESTATIC in case of an internal error.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param expr Expression to expand.
 *
 * \sa cheax_macroexpand()
 */
CHX_API struct chx_value cheax_macroexpand_once(CHEAX *c, struct chx_value expr);

CHX_API struct chx_value cheax_preproc(CHEAX *c, struct chx_value expr);

/*! \brief Evaluates given cheax expression.
 *
 * Core element of the read, eval, print loop.
 *
 * \note This function may call cheax_gc(). Make sure to cheax_ref()
 *       your values properly.
 *
 * \param expr Cheax expression to evaluate.
 *
 * \returns The evaluated expression.
 *
 * \sa cheax_read(), cheax_print()
 */
CHX_API struct chx_value cheax_eval(CHEAX *c, struct chx_value expr);

/*! \brief Invokes function with given argument list.
 *
 * Argument list will be passed to the function as-is. I.e. the
 * individual list nodes will not be evaluated.
 *
 * Throws \ref CHEAX_ETYPE is \a func is not \ref CHEAX_FUNC or
 * \ref CHEAX_EXT_FUNC.
 *
 * \param func Function to invoke.
 * \param list Argument list to pass to \a func.
 *
 * \returns Function return value.
 */
CHX_API struct chx_value cheax_apply(CHEAX *c, struct chx_value func, struct chx_list *list);

/*! \brief Prints given value to file.
 *
 * Core element of the read, eval, print loop.
 *
 * \param output Output file handle.
 * \param expr   Value to print.
 *
 * \sa cheax_format(), cheax_read(), cheax_eval()
 */
CHX_API void cheax_print(CHEAX *c, FILE *output, struct chx_value expr);

/*! \brief Expresses given cheax values as a \a chx_string, using given
 *         format string.
 *
 * Sets cheax_errno() to
 * \li \ref CHEAX_EAPI if \a fmt is NULL;
 * \li \ref CHEAX_EVALUE if format string is otherwise invalid; or
 * \li \ref CHEAX_EINDEX if an index occurs in the format string
 *     (either implicitly or explicitly) that is out of bounds for
 *     \a args.
 *
 * \param fmt  Python-esque format string.
 * \param args List of arguments to format into the output string.
 *
 * \returns A \a chx_string representing the formatted result, or NULL
 *          if an error occurred.
 *
 * \sa cheax_print()
 */
CHX_API struct chx_value cheax_format(CHEAX *c, struct chx_string *fmt, struct chx_list *args);

/*! \brief Reads a file and executes it.
 *
 * \param f Input file path.
 */
CHX_API void cheax_exec(CHEAX *c, const char *f);

CHX_API void *cheax_malloc(CHEAX *c, size_t size);
CHX_API void *cheax_calloc(CHEAX *c, size_t nmemb, size_t size);
CHX_API void *cheax_realloc(CHEAX *c, void *ptr, size_t size);
CHX_API void cheax_free(CHEAX *c, void *ptr);
CHX_API void cheax_gc(CHEAX *c);

/*! @} */

#endif
