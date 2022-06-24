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
 * \sa cheax_new_type(), cheax_type_of()
 */
enum {
	CHEAX_LIST,       /*!< List type. */
	CHEAX_INT,        /*!< Integral type. */
	CHEAX_BOOL,       /*!< Boolean type. */
	CHEAX_DOUBLE,     /*!< Floating point type. */

	/*! \brief Type of user pointers defined from outside the cheax
	 * environment.
	 *
	 * \note Objects of this basic type exist only through type
	 * aliases made with cheax_new_type(), and do not exist as
	 * 'bare' user pointers.
	 */
	CHEAX_USER_PTR,

	CHEAX_ID,         /*!< Identifier type. */
	CHEAX_FUNC,       /*!< Function type. */
	CHEAX_MACRO,      /*!< Macro type. */
	CHEAX_EXT_FUNC,   /*!< Type of functions defined from outside the cheax environment. */
	CHEAX_QUOTE,      /*!< Type of quoted expressions. */
	CHEAX_BACKQUOTE,  /*!< Type of backquoted expressions. */
	CHEAX_COMMA,      /*!< Type of comma expressions. */
	CHEAX_SPLICE,     /*!< Type of comma splice (i.e. ,@) expressions. */
	CHEAX_STRING,     /*!< String type. */
	CHEAX_ENV,        /*!< Environment type. */

	CHEAX_LAST_BASIC_TYPE = CHEAX_ENV,
	CHEAX_TYPESTORE_BIAS,

	/*! The type of type codes themselves. A type alias of \ref CHEAX_INT. */
	CHEAX_TYPECODE = CHEAX_TYPESTORE_BIAS + 0,
	CHEAX_ERRORCODE, /*!< Error code type. A type alias of \ref CHEAX_INT. */
};

#define CHX_INT_MIN INT_LEAST64_MIN
#define CHX_INT_MAX INT_LEAST64_MAX

typedef int_least64_t chx_int;
typedef double chx_double;
struct chx_list;
struct chx_id;
struct chx_string;
struct chx_quote;
struct chx_func;
struct chx_ext_func;
struct chx_env;

/*! \brief Base type of cheax expressions. */
struct chx_value {
	int type;
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
		struct chx_env *as_env;
		void *user_ptr;

		unsigned *rtflags_ptr;
#endif
	union {
		chx_int as_int;
		chx_double as_double;
		struct chx_list *as_list;
		struct chx_id *as_id;
		struct chx_string *as_string;
		struct chx_quote *as_quote;
		struct chx_func *as_func;
		struct chx_ext_func *as_ext_func;
		struct chx_env *as_env;
		void *user_ptr;

		unsigned *rtflags_ptr;
	} data;
#if __STDC_VERSION__ + 0 >= 201112L
	};
#endif
};

#define cheax_nil() ((struct chx_value){ .type = CHEAX_LIST, .data.as_list = NULL })

CHX_API bool cheax_is_nil(struct chx_value v);

struct chx_id {
	unsigned rtflags;
	char *value;
};
struct chx_quote {
	unsigned rtflags;
	struct chx_value value;
};

/*! \brief Creates a cheax identifier expression.
 *
 * \param c  Virtual machine instance.
 * \param id Identifier value for the expression.
 *
 * \sa cheax_id(), CHEAX_ID
 */
CHX_API struct chx_value cheax_id(CHEAX *c, const char *id);

#define cheax_id_value(X) ((struct chx_value){ .type = CHEAX_ID, .data.as_id = (X) })
CHX_API struct chx_value cheax_id_value_proc(struct chx_id *id);

/*! \brief Creates a cheax integer expression.
 *
 * \param value Integral value for the expression.
 *
 * \sa chx_int, CHEAX_INT
 */
#define cheax_int(X) ((struct chx_value){ .type = CHEAX_INT, .data.as_int = (X) })
CHX_API struct chx_value cheax_int_proc(chx_int value);

/*! \brief Creates a cheax boolean with value true.
 *
 * \sa cheax_false(), cheax_bool(), chx_int, CHEAX_BOOL
 */
#define cheax_true() ((struct chx_value){ .type = CHEAX_BOOL, .data.as_int = 1 })

/*! \brief Creates a cheax boolean with value false.
 *
 * \sa cheax_true(), cheax_bool(), chx_int, CHEAX_BOOL
 */
#define cheax_false() ((struct chx_value){ .type = CHEAX_BOOL, .data.as_int = 0 })

/*! \brief Creates a cheax boolean expression.
 *
 * \param value Boolean value for the expression.
 *
 * \sa cheax_true(), cheax_false(), chx_int, CHEAX_BOOL
 */
#define cheax_bool(X) ((struct chx_value){ .type = CHEAX_BOOL, .data.as_int = (X) ? 1 : 0 })
CHX_API struct chx_value cheax_bool_proc(bool value);

/*! \brief Creates a cheax floating point expression.
 *
 * \param value Floating point value for the expression.
 *
 * \sa chx_double, CHEAX_DOUBLE
 */
#define cheax_double(X) ((struct chx_value){ .type = CHEAX_DOUBLE, .data.as_double = (X) })
CHX_API struct chx_value cheax_double_proc(chx_double value);

/*! \brief Cheax s-expression.
 * \sa cheax_list(), CHEAX_LIST
 */
struct chx_list {
	unsigned rtflags;
	struct chx_value value; /*!< The value of the s-expression node. */
	struct chx_list *next;  /*!< The next node in the s-expression. */
};

/*! \brief Creates a cheax s-expression.
 *
 * \param c   Virtual machine instance.
 * \param car Node value.
 * \param cdr Next node or \a NULL.
 *
 * \sa chx_list, CHEAX_LIST
 */
CHX_API struct chx_value cheax_list(CHEAX *c, struct chx_value car, struct chx_list *cdr);

#define cheax_list_value(X) ((struct chx_value){ .type = CHEAX_LIST, .data.as_list = (X) })
CHX_API struct chx_value cheax_list_value_proc(struct chx_list *list);

/*! \brief Cheax function or macro lambda expression.
 *
 * Functions are created with the <tt>(fn args body)</tt> built-in, and
 * macros with the <tt>(macro args body)</tt> built-in. The two types of
 * lambdas differ in whether their argument list is evaluated or not
 * when the lambda is invoked. For functions (most common), all
 * arguments are pre-evaluated, for macros they are not.
 *
 * Lambdas cannot be constructed by the C API.
 *
 * \sa CHEAX_FUNC, CHEAX_MACRO, chx_ext_func, cheax_ext_func()
 */
struct chx_func {
	unsigned rtflags;
	struct chx_value args;       /*!< Lambda argument list expression. */
	struct chx_list *body;       /*!< Lambda body. */
	struct chx_env *lexenv;      /*!< Lexical environment. \note Internal use only. */
};

#define cheax_func_value(X) ((struct chx_value){ .type = CHEAX_FUNC, .data.as_func = (X) })
CHX_API struct chx_value cheax_func_value_proc(struct chx_func *fn);
#define cheax_macro_value(X) ((struct chx_value){ .type = CHEAX_MACRO, .data.as_func = (X) })
CHX_API struct chx_value cheax_macro_value_proc(struct chx_func *macro);

/*! \brief Type for C functions to be invoked from cheax.
 *
 * \param c    Virtual machine instance.
 * \param args The argument list as the function was invoked. The
 *             arguments are given as is, not pre-evaluated. E.g. if
 *             cheax passes an identifier to the function as an
 *             argument, it will apear as an identifier in the argument
 *             list, not as the value of the symbol it may represent.
 * \param info User info.
 *
 * \returns The function's return value to be delivered back to cheax.
 *
 * \sa chx_ext_func, cheax_ext_func(), cheax_defmacro()
 */
typedef struct chx_value (*chx_func_ptr)(CHEAX *c, struct chx_list *args, void *info);

/*! \brief Cheax external/user function expression.
 * \sa cheax_ext_func(), CHEAX_EXT_FUNC, cheax_defmacro(), chx_func_ptr
 */
struct chx_ext_func {
	unsigned rtflags;
	const char *name;      /*!< The function's name, used by cheax_print(). */
	chx_func_ptr perform;  /*!< The function pointer to be invoked. */
	void *info;            /*!< Callback info to be passed upon invocation. */
};

/*! \brief Creates a cheax external/user function expression.
 *
 * \param c       Virtual machine instance.
 * \param perform Function pointer to be invoked.
 * \param name    Function name as will be used by cheax_print().
 * \param info    Callback info to be passed upon invocation.
 */
CHX_API struct chx_value cheax_ext_func(CHEAX *c,
                                        const char *name,
                                        chx_func_ptr perform,
                                        void *info);

#define cheax_ext_func_value(X) ((struct chx_value){ .type = CHEAX_EXT_FUNC, .data.as_ext_func = (X) })
CHX_API struct chx_value cheax_ext_func_value_proc(struct chx_ext_func *extf);

/*! \brief Creates a quoted cheax expression.
 *
 * \param c     Virtual machine instance.
 * \param value Expression to be quoted.
 *
 * \sa chx_quote, CHEAX_QUOTE
 */
CHX_API struct chx_value cheax_quote(CHEAX *c, struct chx_value value);

#define cheax_quote_value(X) ((struct chx_value){ .type = CHEAX_QUOTE, .data.as_quote = (X) })
CHX_API struct chx_value cheax_quote_value_proc(struct chx_quote *quote);

/*! \brief Creates a backquoted cheax expression.
 *
 * \param c     Virtual machine instance.
 * \param value Expression to be backquoted.
 *
 * \sa chx_quote, CHEAX_BACKQUOTE
 */
CHX_API struct chx_value cheax_backquote(CHEAX *c, struct chx_value value);

#define cheax_backquote_value(X) ((struct chx_value){ .type = CHEAX_BACKQUOTE, .data.as_quote = (X) })
CHX_API struct chx_value cheax_backquote_value_proc(struct chx_quote *bkquote);

/*! \brief Creates a cheax comma expression.
 *
 * \param c     Virtual machine instance.
 * \param value Expression following comma.
 *
 * \sa chx_quote, CHEAX_COMMA
 */
CHX_API struct chx_value cheax_comma(CHEAX *c, struct chx_value value);

#define cheax_comma_value(X) ((struct chx_value){ .type = CHEAX_COMMA, .data.as_quote = (X) })
CHX_API struct chx_value cheax_comma_value_proc(struct chx_quote *comma);

/*! \brief Creates a cheax comma splice expression.
 *
 * \param c     Virtual machine instance.
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
 * \param c   Virtual machine instance.
 * \param str String.
 *
 * \returns Size of given string, or zero if \a str is \a NULL.
 */
CHX_API size_t cheax_strlen(CHEAX *c, struct chx_string *str);

/*! \brief Creates a cheax string expression.
 *
 * \param c     Virtual machine instance.
 * \param value Null-terminated value for the string.
 *
 * \sa chx_string, cheax_nstring(), CHEAX_STRING
 */
CHX_API struct chx_value cheax_string(CHEAX *c, const char *value);

/*! \brief Creates a cheax string expression of given length.
 *
 * \param c     Virtual machine instance.
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
 * \param c   Virtual machine instance.
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
 * \returns Null terminated string or \a NULL or \a str is \a NULL.
 */
CHX_API char *cheax_strdup(struct chx_string *str);

/*! \brief Creates a cheax user pointer expression.
 *
 * \param c     Virtual machine instance.
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
 * \returns Currently active \ref chx_env, or \a NULL if currently
 *          running in the global scope.
 */
CHX_API struct chx_value cheax_env(CHEAX *c);

#define cheax_env_value(X) ((struct chx_value){ .type = CHEAX_ENV, .data.as_env = (X) })
CHX_API struct chx_value cheax_env_value_proc(struct chx_env *env);

#if __STDC_VERSION__ + 0 >= 201112L
#define cheax_value(v)                                                \
	(_Generic((0,v),                                              \
		int:                    cheax_int_proc,               \
		chx_int:                cheax_int_proc,               \
		double:                 cheax_double_proc,            \
		float:                  cheax_double_proc,            \
		struct chx_ext_func *:  cheax_ext_func_value_proc,    \
		struct chx_env *:       cheax_env_value_proc,         \
		struct chx_id *:        cheax_id_value_proc,          \
		struct chx_string *:    cheax_string_value_proc,      \
		struct chx_list *:      cheax_list_value_proc)(v))
#endif

struct chx_sym;

typedef struct chx_value (*chx_getter)(CHEAX *c, struct chx_sym *sym);
typedef void (*chx_setter)(CHEAX *c, struct chx_sym *sym, struct chx_value value);
typedef void (*chx_finalizer)(CHEAX *c, struct chx_sym *sym);

struct chx_sym {
	void *user_info;
	chx_getter get;
	chx_setter set;
	chx_finalizer fin;
	struct chx_value protect;
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
 * \li \a name is \a NULL;
 * \li \a base_type is not a valid type code;
 * \li or \a name already names a type.
 *
 * \param c         Virtual machine instance.
 * \param name      Name for the new type code in the cheax environment.
 * \param base_type Base type code to create an alias for.
 *
 * \returns The new type code. -1 if unsuccesful.
 */
CHX_API int cheax_new_type(CHEAX *c, const char *name, int base_type);

/*! \brief Looks up the type code of a named type.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a name is NULL.
 *
 * \param c    Virtual machine instance.
 * \param name Type name.
 *
 * \returns The type code of a type with the given name, or -1 if
 *          unsuccesful.
 */
CHX_API int cheax_find_type(CHEAX *c, const char *name);

/*! \brief Checks whether a given type code is valid.
 *
 * \param c    Virtual machine instance.
 * \param type Code to check.
 *
 * \returns Whether \a type is a valid type code.
 */
CHX_API bool cheax_is_valid_type(CHEAX *c, int type);

/*! \brief Checks whether a given type code is a basic type.
 *
 * \param c    Virtual machine instance.
 * \param type Type code to check.
 *
 * \returns Whether \a type is a basic type.
 */
CHX_API bool cheax_is_basic_type(CHEAX *c, int type);

/*! \brief Checks whether a given type code is a user-defined type code.
 *
 * \param c    Virtual machine instance.
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
 * \param c    Virtual machine instance.
 * \param type Type code to get the base type of.
 *
 * \returns The base type of \a type, or -1 if unsuccesful.
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
 * \param c    Virtual machine instance.
 * \param type Type code to resolve.
 *
 * \returns The basic type to which \a type refers, or -1 if unsuccesful.
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
	/* CHEAX_ENIL was removed */
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
	ERR_NAME_PAIR(EMATCH), /* ENIL removed */             \
	ERR_NAME_PAIR(EDIVZERO), ERR_NAME_PAIR(EREADONLY),    \
	ERR_NAME_PAIR(EWRITEONLY), ERR_NAME_PAIR(EEXIST),     \
	ERR_NAME_PAIR(EVALUE), ERR_NAME_PAIR(EOVERFLOW),      \
	ERR_NAME_PAIR(EINDEX), ERR_NAME_PAIR(EIO),            \
	                                                      \
	ERR_NAME_PAIR(EAPI), ERR_NAME_PAIR(ENOMEM)            \
}

/*! \brief Gets the value of the current cheax error code.
 *
 * \param c Virtual machine instance.
 *
 * \sa cheax_throw(), cheax_new_error_code(), cheax_ft()
 */
CHX_API int cheax_errno(CHEAX *c);

/*! \brief Macro to fall through to a pad in case of an error.
 *
 * Jumps to \a pad if cheax_errno() is not 0. Most commonly used after
 * cheax_eval().
 *
 * \param c   Virtual machine instance.
 * \param pad Label to jump to in case of an error.
 */
#define cheax_ft(c, pad) { if (cheax_errno(c) != 0) goto pad; }

/*! \brief Prints the current cheax error code and error message.
 *
 * \param c Virtual machine instance.
 * \param s Argument string, will be printed followed by a colon and an
 *          error description.
 */
CHX_API void cheax_perror(CHEAX *c, const char *s);

/*! \brief Sets cheax_errno() to 0.
 *
 * \param c Virtual machine instance.
 *
 * \sa cheax_errno()
 */
CHX_API void cheax_clear_errno(CHEAX *c);

/*! \brief Sets cheax_errno() to the given value.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a code is 0.
 *
 * \param c    Virtual machine instance.
 * \param code Error code.
 * \param msg  Error message string or \a NULL.
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
 * \param c    Virtual machine instance
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
 * \param c    Virtual machine instance.
 * \param name Error code name.
 *
 * \returns The error code carrying the given name, or -1 if
 *          unsuccesful.
 */
CHX_API int cheax_find_error_code(CHEAX *c, const char *name);

/*! @} */

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
 * \li \c "file-io" to load \c fopen and \c fclose built-ins;
 * \li <tt>"set-max-stack-depth"</tt> to load the <tt>set-max-stack-depth</tt> built-in;
 * \li \c "gc" to load the \c gc built-in function;
 * \li \c "exit" to load the \c exit function;
 * \li \c "stdin" to expose the \c stdin variable;
 * \li \c "stdout" to expose the \c stdout variable;
 * \li \c "stderr" to expose the \c stderr variable;
 * \li \c "stdio" to expose \c stdin, \c stdout and \c stderr;
 * \li \c "all" to load every feature available (think twice before using).
 *
 * A feature can only be loaded once. Attempting to load a feature more
 * than once will cause no action.
 *
 * \param c    Virtual machine instance.
 * \param feat Which feature to load.
 *
 * \returns 0 if the given feature was loaded succesfully, or -1 if the
 *          given feature is not supported.
 */
CHX_API int cheax_load_feature(CHEAX *c, const char *feat);

/*! \brief Loads the cheax standard library.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if the standard library
 * could not be found.
 *
 * \param c Virtual machine instance.
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
 * \param c Virtual machine instance to be destroyed.
 */
CHX_API void cheax_destroy(CHEAX *c);

/*! \brief Converts chx_list to array.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a array_ptr or \a length
 * is NULL, or to \ref CHEAX_ENOMEM if array allocation failed.
 *
 * \param c         Virtual machine instance.
 * \param list      List to convert.
 * \param array_ptr Output parameter, will point to array of cheax_value
 *                  pointers. Make sure to cheax_free() after use.
 * \param length    Output parameter, will point to length of output
 *                  array.
 *
 * \returns 0 if everything succeeded without errors, -1 if there was an
 *          error (most likely \ref CHEAX_ENOMEM).
 */
CHX_API int cheax_list_to_array(CHEAX *c,
                                struct chx_list *list,
                                struct chx_value **array_ptr,
                                size_t *length);
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
 * \param c  Virtual machine instance.
 */
CHX_API void cheax_push_env(CHEAX *c);

/*! \brief Pushes new bifurcated environment to environment stack.
 *
 * Lookups will first look in \a main, then look further down the stack.
 * New symbols are declared only in \a main.
 *
 * \param c     Virtual machine instance.
 * \param main  Main branch of bifurcated environment.
 */
CHX_API void cheax_enter_env(CHEAX *c, struct chx_env *main);

/*! \brief Pops environment off environment stack.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if environment stack is empty.
 *
 * \param c  Virtual machine instance.
 */
CHX_API void cheax_pop_env(CHEAX *c);

CHX_API struct chx_sym *cheax_defsym(CHEAX *c, const char *id,
                                     chx_getter get, chx_setter set,
                                     chx_finalizer fin, void *user_info);

/*! \brief Creates a new symbol in the cheax environment.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a id is \a NULL, or to
 * \ref CHEAX_EEXIST if a symbol with name \a id already exists.
 *
 * \param c      Virtual machine instance.
 * \param id     Variable identifier.
 * \param value  Initial value or the symbol. May be \a NULL.
 * \param flags  Variable flags. Use 0 if there are no special needs.
 *
 * \sa cheax_get(), cheax_set()
 */
CHX_API void cheax_def(CHEAX *c, const char *id, struct chx_value value, int flags);

/*! \brief Retrieves the value of the given symbol.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a id is \a NULL, or to
 * \ref CHEAX_ENOSYM if no symbol with name \a id could be found.
 *
 * \param c  Virtual machine instance.
 * \param id Identifier to look up.
 *
 * \returns The value of the given symbol. Always \a NULL in case of an
 *          error.
 *
 * \sa cheax_get_from(), cheax_set()
 */
CHX_API struct chx_value cheax_get(CHEAX *c, const char *id);

/*! \brief Retrieves the value of the given symbol, performing symbol
 *         lookup only in the specified environment.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a id is \a NULL, or to
 * \ref CHEAX_ENOSYM if no symbol with name \a id could be found.
 *
 * \param c   Virtual machine instance.
 * \param env Environment to look up identifier in.
 * \param id  Identifier to look up.
 *
 * \returns The value of the given symbol. Always \a NULL in case of an
 *          error.
 *
 * \sa cheax_get(), cheax_set()
 */
CHX_API struct chx_value cheax_get_from(CHEAX *c, struct chx_env *env, const char *id);

/*! \brief Sets the value of a symbol.
 *
 * Sets cheax_errno() to
 * \li \ref CHEAX_EAPI if \a id is \a NULL;
 * \li \ref CHEAX_ENOSYM if no symol with name \a id could be found;
 * \li \ref CHEAX_EREADONLY if the given symbol was declared read-only;
 * \li or \ref CHEAX_ETYPE if symbol is synchronised and type of
 *     \a value is invalid.
 *
 * cheax_set() cannot be used to declare a new symbol, use
 * cheax_def() instead.
 *
 * \param c     Virtual machine instance.
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
 * \param c       Virtual machine instance.
 * \param id      Identifier for the external function.
 * \param perform Callback for the new external function.
 * \param info    Callback info for the new external function.
 *
 * \sa chx_ext_func, cheax_ext_func(), cheax_def()
 */
CHX_API void cheax_defmacro(CHEAX *c, const char *id, chx_func_ptr perform, void *info);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param c     Virtual machine instance.
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_int(CHEAX *c, const char *name, int *var, int flags);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param c     Virtual machine instance.
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_bool(CHEAX *c, const char *name, bool *var, int flags);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param c     Virtual machine instance.
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_float(CHEAX *c, const char *name, float *var, int flags);

/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param c     Virtual machine instance.
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_double(CHEAX *c, const char *name, double *var, int flags);

/*! \brief Synchronizes a null-terminated string buffer from C with a
 *         symbol in the cheax environment.
 *
 * \param c     Virtual machine instance.
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
 * \param c     Virtual machine instance.
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
 * \param c     Virtual machine instance.
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
 * \param c Virtual machine instance.
 * \param l Left.
 * \param r Right.
 *
 * \returns Whether the given cheax expressions are equal in value.
 */
CHX_API bool cheax_eq(CHEAX *c, struct chx_value l, struct chx_value r);

/*! \brief Attempts to cast an expression to a given type.
 *
 * Sets cheax_errno() to \ref CHEAX_ETYPE if casting is not possible.
 *
 * \param c    Virtual machine instance.
 * \param v    Expression to cast.
 * \param type Code of the type to cast to.
 *
 * \returns The cast expression. Always \a NULL if unsuccesful.
 */
CHX_API struct chx_value cheax_cast(CHEAX *c, struct chx_value v, int type);

/* \brief Get value of integer configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if no option of integer type
 * with name \a opt exists.
 *
 * \param c   Virtual machine instance.
 * \param opt Option name.
 *
 * \returns Option value, or 0 upon failure.
 */
CHX_API int cheax_config_get_int(CHEAX *c, const char *opt);

/* \brief Set value of integer configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if option \a opt does not have
 * integer type, or if \a value is otherwise invalid for option \a opt.
 * Fails silently in case no option with name \a opt could be found.
 *
 * \param c     Virtual machine instance.
 * \param opt   Option name.
 * \param value Option value.
 *
 * \returns 0 if an integer option with name \a opt was found, -1 otherwise.
 */
CHX_API int cheax_config_int(CHEAX *c, const char *opt, int value);

/* \brief Get value of boolean configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if no option of boolean type
 * with name \a opt exists.
 *
 * \param c   Virtual machine instance.
 * \param opt Option name.
 *
 * \returns Option value, or \c false upon failure.
 */
CHX_API bool cheax_config_get_bool(CHEAX *c, const char *opt);

/* \brief Set value of boolean configuration option.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if option \a opt does not have
 * boolean type, or if \a value is otherwise invalid for option \a opt.
 * Fails silently in case no option with name \a opt could be found.
 *
 * \param c     Virtual machine instance.
 * \param opt   Option name.
 * \param value Option value.
 *
 * \returns 0 if a boolean option with name \a opt was found, -1 otherwise.
 */
CHX_API int cheax_config_bool(CHEAX *c, const char *opt, bool value);

/* \brief Information about cheax config option. */
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

/*! \brief Reads cheax expression from file.
 *
 * Core element of the read, eval, print loop.
 *
 * \param c Virtual machine instance.
 * \param f File handle to read from.
 *
 * \returns The cheax expression that was read from the file. \a NULL
 *          if unsuccesful.
 *
 * \sa cheax_readstr(), cheax_eval(), cheax_print()
 */
CHX_API struct chx_value cheax_read(CHEAX *c, FILE *f);
CHX_API struct chx_value cheax_read_at(CHEAX *c,
                                       FILE *f,
                                       const char *path,
                                       int *line,
                                       int *pos);

/*! \brief Reads cheax expression from string.
 *
 * Core element of the read, eval, print loop.
 *
 * \param c   Virtual machine instance.
 * \param str String to read from.
 *
 * \returns The cheax expression that was read from the string. \a NULL
 *          if unsuccesful.
 *
 * \sa cheax_read(), cheax_eval(), cheax_print()
 */
CHX_API struct chx_value cheax_readstr(CHEAX *c, const char *str);
CHX_API struct chx_value cheax_readstr_at(CHEAX *c,
                                          const char **str,
                                          const char *path,
                                          int *line,
                                          int *pos);

/*! \brief Evaluates given cheax expression.
 *
 * Core element of the read, eval, print loop.
 *
 * \param c    Virtual machine instance.
 * \param expr Cheax expression to evaluate.
 *
 * \returns The evaluated expression.
 *
 * \sa cheax_read(), cheax_print()
 */
CHX_API struct chx_value cheax_eval(CHEAX *c, struct chx_value expr);

/*! \brief Prints given cheax expression to file.
 *
 * Core element of the read, eval, print loop.
 *
 * \param c      Virtual machine instance.
 * \param output Output file handle.
 * \param expr   Cheax expression to print.
 *
 * \sa cheax_format(), cheax_read(), cheax_eval()
 */
CHX_API void cheax_print(CHEAX *c, FILE *output, struct chx_value expr);

/*! \brief Expresses given cheax values as a \a chx_string, using given
 *         format string.
 *
 * Sets cheax_errno() to
 * \li \ref CHEAX_EAPI if \a fmt is NULL;
 * \li \ref CHEAX_EVALUE if format string is otherwise invalid;
 * \li or \ref CHEAX_EINDEX if an index occurs in the format string
 *     (either implicitly or explicitly) that is out of bounds for
 *     \a args.
 *
 * \param c      Virtual machine instance.
 * \param fmt    Python-esque format string.
 * \param args   List of arguments to format into the output string.
 *
 * \returns A \a chx_string representing the formatted result, or NULL
 *          if an error occurred.
 *
 * \sa cheax_print()
 */
CHX_API struct chx_value cheax_format(CHEAX *c, struct chx_string *fmt, struct chx_list *args);

/*! \brief Reads a file and executes it.
 *
 * \param c Virtual machine instance.
 * \param f Input file path.
 */
CHX_API void cheax_exec(CHEAX *c, const char *f);

CHX_API void *cheax_malloc(CHEAX *c, size_t size);
CHX_API void *cheax_calloc(CHEAX *c, size_t nmemb, size_t size);
CHX_API void *cheax_realloc(CHEAX *c, void *ptr, size_t size);
CHX_API void cheax_free(CHEAX *c, void *ptr);

/*! @} */

#endif
