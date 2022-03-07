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

#include <stdio.h>
#include <stdbool.h>

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
	CHEAX_NIL,        /*!< Type of the empty list, nil. */
	CHEAX_ID,         /*!< Identifier type. */
	CHEAX_INT,        /*!< Integral type. */
	CHEAX_DOUBLE,     /*!< Floating point type. */
	CHEAX_BOOL,       /*!< Boolean type. */
	CHEAX_LIST,       /*!< List type. */
	CHEAX_FUNC,       /*!< Function type. */
	CHEAX_MACRO,      /*!< Macro type. */
	CHEAX_EXT_FUNC,   /*!< Type of functions defined from outside the cheax environment. */
	CHEAX_QUOTE,      /*!< Type of quoted expressions. */
	CHEAX_BACKQUOTE,  /*!< Type of backquoted expressions. */
	CHEAX_COMMA,      /*!< Type of comma expressions. */
	CHEAX_STRING,     /*!< String type. */
	CHEAX_ENV,        /*!< Environment type. */

	/*! \brief Type of user pointers defined from outside the cheax
	 * environment.
	 *
	 * \note Objects of this basic type exist only through type
	 * aliases made with cheax_new_type(), and do not exist as
	 * 'bare' user pointers.
	 */
	CHEAX_USER_PTR,

	CHEAX_LAST_BASIC_TYPE = CHEAX_USER_PTR,
	CHEAX_TYPESTORE_BIAS,

	/*! The type of type codes themselves. A type alias of \ref CHEAX_INT. */
	CHEAX_TYPECODE = CHEAX_TYPESTORE_BIAS + 0,
	CHEAX_ERRORCODE, /*!< Error code type. A type alias of \ref CHEAX_INT. */

	CHEAX_TYPE_MASK = 0xFFFFFF,
};

/*! \brief Base type of cheax expressions. */
struct chx_value {
	/*! Hands off! Some bits are used as internal flags. Always use
	 * cheax_type_of().
	 * \note Internal use only.
	 * \sa cheax_type_of()
	 */
	int type;
};

/*! \brief Cheax identifier expression.
 * \sa cheax_id(), CHEAX_ID
 */
struct chx_id {
	struct chx_value base; /*!< Base. */
	char *id;              /*!< The identifier's value. */
};

/*! \brief Creates a cheax identifier expression.
 *
 * \param c  Virtual machine instance.
 * \param id Identifier value for the expression.
 *
 * \sa cheax_id(), CHEAX_ID
 */
CHX_API struct chx_id *cheax_id(CHEAX *c, const char *id);

/*! \brief Cheax integer or boolean expression.
 * \sa cheax_int(), CHEAX_INT, cheax_bool(), CHEAX_BOOL
 */
struct chx_int {
	struct chx_value base; /*!< Base. */
	int value;             /*!< The integer's value. */
};

/*! \brief Creates a cheax integer expression.
 *
 * \param c     Virtual machine instance.
 * \param value Integral value for the expression.
 *
 * \sa chx_int, CHEAX_INT
 */
CHX_API struct chx_int *cheax_int(CHEAX *c, int value);

/*! \brief Creates a cheax boolean with value true.
 *
 * \param c     Virtual machine instance.
 *
 * \sa cheax_false(), cheax_bool(), chx_int, CHEAX_BOOL
 */
CHX_API struct chx_int *cheax_true(CHEAX *c);

/*! \brief Creates a cheax boolean with value false.
 *
 * \param c     Virtual machine instance.
 *
 * \sa cheax_true(), cheax_bool(), chx_int, CHEAX_BOOL
 */
CHX_API struct chx_int *cheax_false(CHEAX *c);

/*! \brief Creates a cheax boolean expression.
 *
 * \param c     Virtual machine instance.
 * \param value Boolean value for the expression.
 *
 * \sa cheax_true(), cheax_false(), chx_int, CHEAX_BOOL
 */
CHX_API struct chx_int *cheax_bool(CHEAX *c, bool value);

/*! \brief Cheax floating point expression.
 * \sa cheax_double(), CHEAX_DOUBLE
 */
struct chx_double {
	struct chx_value base; /*!< Base. */
	double value;          /*!< The floating point's value. */
};

/*! \brief Creates a cheax floating point expression.
 *
 * \param c     Virtual machine instance.
 * \param value Floating point value for the expression.
 *
 * \sa chx_double, CHEAX_DOUBLE
 */
CHX_API struct chx_double *cheax_double(CHEAX *c, double value);

/*! \brief Cheax s-expression.
 * \sa cheax_list(), CHEAX_LIST
 */
struct chx_list {
	struct chx_value base;   /*!< Base. */
	struct chx_value *value; /*!< The value of the s-expression node. */
	struct chx_list *next;   /*!< The next node in the s-expression. */
};

/*! \brief Creates a cheax s-expression.
 *
 * \param c   Virtual machine instance.
 * \param car Node value.
 * \param cdr Next node or \a NULL.
 *
 * \sa chx_list, CHEAX_LIST
 */
CHX_API struct chx_list *cheax_list(CHEAX *c, struct chx_value *car, struct chx_list *cdr);

/*! \brief Cheax function or macro lambda expression.
 *
 * Functions are created with the <tt>(\\ args body)</tt> built-in, and
 * macros with the <tt>(\\\\ args body)</tt> built-in. The two types of
 * lambdas differ in whether their argument list is evaluated or not
 * when the lambda is invoked. For functions (most common), all
 * arguments are pre-evaluated, for macros they are not.
 *
 * Lambdas cannot be constructed by the C API.
 *
 * \sa CHEAX_FUNC, CHEAX_MACRO, chx_ext_func, cheax_ext_func()
 */
struct chx_func {
	struct chx_value base;        /*!< Base. */
	struct chx_value *args;       /*!< Lambda argument list expression. */
	struct chx_list *body;        /*!< Lambda body. */
	struct chx_env *lexenv;       /*!< Lexical environment. \note Internal use only. */
};

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
typedef struct chx_value *(*chx_func_ptr)(CHEAX *c, struct chx_list *args, void *info);

/*! \brief Cheax external/user function expression.
 * \sa cheax_ext_func(), CHEAX_EXT_FUNC, cheax_defmacro(), chx_func_ptr
 */
struct chx_ext_func {
	struct chx_value base; /*!< Base. */
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
CHX_API struct chx_ext_func *cheax_ext_func(CHEAX *c,
                                            const char *name,
                                            chx_func_ptr perform,
                                            void *info);

/*! \brief Quoted cheax expression.
 * \sa cheax_quote(), CHEAX_QUOTE, cheax_backquote(), CHEAX_BACKQUOTE,
 *     cheax_comma(), CHEAX_COMMA
 */
struct chx_quote {
	struct chx_value base;   /*!< Base. */
	struct chx_value *value; /*!< The quoted expression. */
};

/*! \brief Creates a quoted cheax expression.
 *
 * \param c     Virtual machine instance.
 * \param value Expression to be quoted.
 *
 * \sa chx_quote, CHEAX_QUOTE
 */
CHX_API struct chx_quote *cheax_quote(CHEAX *c, struct chx_value *value);

/*! \brief Creates a backquoted cheax expression.
 *
 * \param c     Virtual machine instance.
 * \param value Expression to be backquoted.
 *
 * \sa chx_quote, CHEAX_BACKQUOTE
 */
CHX_API struct chx_quote *cheax_backquote(CHEAX *c, struct chx_value *value);

/*! \brief Creates a cheax comma expression.
 *
 * \param c     Virtual machine instance.
 * \param value Expression following comma.
 *
 * \sa chx_quote, CHEAX_COMMA
 */
CHX_API struct chx_quote *cheax_comma(CHEAX *c, struct chx_value *value);

/*! \brief Cheax string expression.
 * \sa cheax_string(), cheax_nstring(), CHEAX_STRING
 */
struct chx_string {
	struct chx_value base; /*!< Base. */
	char *value;           /*!< Null-terminated string value. */
	size_t len;            /*!< String length. */
	struct chx_string *orig;
};

/*! \brief Creates a cheax string expression.
 *
 * \param c     Virtual machine instance.
 * \param value Null-terminated value for the string.
 *
 * \sa chx_string, cheax_nstring(), CHEAX_STRING
 */
CHX_API struct chx_string *cheax_string(CHEAX *c, const char *value);

/*! \brief Creates a cheax string expression of given length.
 *
 * \param c     Virtual machine instance.
 * \param value Value for the string.
 * \param len   Length of the string.
 *
 * \sa chx_string, cheax_string(), CHEAX_STRING
 */
CHX_API struct chx_string *cheax_nstring(CHEAX *c, const char *value, size_t len);

/*! \brief Takes substring of given cheax string.
 *
 * Sets cheax_errno() to \ref CHEAX_EVALUE if substring is out of bounds.
 *
 * \param c   Virtual machine instance.
 * \param str Initial string.
 * \param pos Substring starting offset in number of bytes.
 * \param len Substring length in number of bytes.
 */
CHX_API struct chx_string *cheax_substr(CHEAX *c, struct chx_string *str, size_t pos, size_t len);

/*! \brief Cheax user pointer expression.
 *
 * A user pointer is used to pass any custom pointer from C to cheax,
 * i.e. file handles or custom objects. For type safety reasons, user
 * pointer expressions with a type code of simply \ref CHEAX_USER_PTR
 * are not allowed.
 *
 * \sa cheax_user_ptr(), CHEAX_USER_PTR
 */
struct chx_user_ptr {
	struct chx_value base; /*!< Base. */
	void *value;           /*!< The user pointer's value. */
};

/*! \brief Creates a cheax user pointer expression.
 *
 * \param c     Virtual machine instance.
 * \param value Pointer value for the expression.
 * \param type  Type alias for the expression. Must not be a basic type,
 *              and must resolve to \ref CHEAX_USER_PTR.
 *
 * \sa chx_user_ptr, CHEAX_USER_PTR
 */
CHX_API struct chx_user_ptr *cheax_user_ptr(CHEAX *c, void *value, int type);

/*! \brief Cheax environment, storing symbols and their values.
 *
 * \sa cheax_push_env(), cheax_enter_env(), cheax_pop_env(),
 *     CHEAX_ENV, cheax_env()
 */
struct chx_env;

struct chx_sym;

typedef struct chx_value *(*chx_getter)(CHEAX *c, struct chx_sym *sym);
typedef void (*chx_setter)(CHEAX *c, struct chx_sym *sym, struct chx_value *value);
typedef void (*chx_finalizer)(CHEAX *c, struct chx_sym *sym);

struct chx_sym {
	void *user_info;
	chx_getter get;
	chx_setter set;
	chx_finalizer fin;
	struct chx_value *protect;
};

typedef bool chx_ref;

/*! \brief Increase reference count on cheax value, preventing it from
 *         gc deletion when cheax_eval() is called.
 * \sa cheax_unref()
 */
CHX_API chx_ref cheax_ref(CHEAX *c, void *restrict value);

/*! \brief Decrease reference count on cheax value, potentially allowing
 *         it to be deleted by gc when cheax_eval() is called.
 * \sa cheax_ref()
 */
CHX_API void cheax_unref(CHEAX *c, void *restrict value, chx_ref ref);

/*! \brief Gets the type code of the given expression.
 *
 * Always use this function instead of examining
 * \ref chx_value::type "v->type" directly.
 *
 * \param v Expression to examine the type of.
 *
 * \returns Type code of \a v.
 *
 * \sa cheax_new_type(), cheax_resolve_type()
 */
CHX_API int cheax_type_of(struct chx_value *v);

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

enum {
	CHEAX_RUNNING,
	CHEAX_THROWN,
};

/*! \brief Indicates whether cheax is running normally or an error has
 *         been thrown.
 *
 * \returns \a CHEAX_RUNNING if running normally or \a CHEAX_THROWN if
 *          an error has been thrown.
 *
 * \sa cheax_ft()
 */
CHX_API int cheax_errstate(CHEAX *c);

/*! \brief Gets the value of the current cheax error code.
 *
 * \param c Virtual machine instance.
 *
 * \sa cheax_throw(), cheax_new_error_code(), cheax_errstate(),
 *     cheax_ft()
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
#define cheax_ft(c, pad) { if (cheax_errstate(c) == CHEAX_THROWN) goto pad; }

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
 *                  pointers. Make sure to free() after use.
 * \param length    Output parameter, will point to length of output
 *                  array.
 *
 * \returns 0 if everything succeeded without errors, -1 if there was an
 *          error (most likely \ref CHEAX_ENOMEM).
 */
CHX_API int cheax_list_to_array(CHEAX *c,
                                struct chx_list *list,
                                struct chx_value ***array_ptr,
                                size_t *length);

/*! \brief Options for symbol declaration.
 *
 * \sa cheax_var(), cheax_match(), cheax_sync_int(), cheax_sync_float(),
 *     cheax_sync_double()
 */
enum {
	CHEAX_SYNCED     = 0x01, /*!< Reserved. \note Unused. */
	CHEAX_READONLY   = 0x02, /*!< Marks a symbol read-only. */
	CHEAX_WRITEONLY  = 0x04, /*!< Marks a symbol write-only. */
};

/*! \brief Pushes new empty environment to environment stack.
 *
 * \param c  Virtual machine instance.
 *
 * \returns New environment.
 */
CHX_API struct chx_env *cheax_push_env(CHEAX *c);

/*! \brief Pushes new bifurcated environment to environment stack.
 *
 * Lookups will first look in \a main, then look further down the stack.
 * New symbols are declared only in \a main.
 *
 * \param c     Virtual machine instance.
 * \param main  Main branch of bifurcated environment.
 *
 * \returns New bifurcated environment.
 */
CHX_API struct chx_env *cheax_enter_env(CHEAX *c, struct chx_env *main);

/*! \brief Pops environment off environment stack.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if environment stack is empty.
 *
 * \param c  Virtual machine instance.
 *
 * \returns Popped environment, or \a NULL in case of an error.
 */
CHX_API struct chx_env *cheax_pop_env(CHEAX *c);

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
CHX_API void cheax_var(CHEAX *c, const char *id, struct chx_value *value, int flags);

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
 * \sa cheax_set()
 */
CHX_API struct chx_value *cheax_get(CHEAX *c, const char *id);

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
 * cheax_var() instead.
 *
 * \param c     Virtual machine instance.
 * \param id    Identifier of the symbol to look up and set.
 * \param value New value for the symbol with the given identifier.
 *
 * \sa cheax_get(), cheax_var()
 */
CHX_API void cheax_set(CHEAX *c, const char *id, struct chx_value *value);

/*! \brief Shorthand function to declare an external function the cheax
 *         environment.
 *
 * Shorthand for:
\code{.c}
cheax_var(c, id, &cheax_ext_func(c, id, perform, info)->base, CHEAX_READONLY);
\endcode
 *
 * \param c       Virtual machine instance.
 * \param id      Identifier for the external function.
 * \param perform Callback for the new external function.
 * \param info    Callback info for the new external function.
 *
 * \sa chx_ext_func, cheax_ext_func(), cheax_var()
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
CHX_API void cheax_sync_float(CHEAX *c, const char *name, float *var, int flags);
/*! \brief Synchronizes a variable from C with a symbol in the cheax environment.
 *
 * \param c     Virtual machine instance.
 * \param name  Identifier for the symbol in the cheax environment.
 * \param var   Reference to the C variable to synchronize.
 * \param flags Symbol flags. Use 0 if there are no special needs.
 */
CHX_API void cheax_sync_double(CHEAX *c, const char *name, double *var, int flags);

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
CHX_API bool cheax_match(CHEAX *c, struct chx_value *pan, struct chx_value *match, int flags);

/*! \brief Tests whether two given cheax expressions are equal.
 *
 * \param c Virtual machine instance.
 * \param l Left.
 * \param r Right.
 *
 * \returns Whether the given cheax expressions are equal in value.
 */
CHX_API bool cheax_eq(CHEAX *c, struct chx_value *l, struct chx_value *r);

/*! \brief Creates a shallow copy of the given cheax expression.
 *
 * \param c Virtual machine instance.
 * \param v Expression to copy.
 */
CHX_API struct chx_value *cheax_shallow_copy(CHEAX *c, struct chx_value *v);

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
CHX_API struct chx_value *cheax_cast(CHEAX *c, struct chx_value *v, int type);

/*! \brief Gets the maximum cheax call stack depth.
 *
 * \param c Virtual machine instance.
 *
 * \returns Maximum cheax call stack depth.
 *
 * \sa cheax_set_max_stack_depth()
 */
CHX_API int cheax_get_max_stack_depth(CHEAX *c);

/*! \brief Sets the maximum cheax call stack depth.
 *
 * Sets cheax_errno() to \ref CHEAX_EAPI if \a max_stack_depth is not
 * positive.
 *
 * \param c               Virtual machine instance.
 * \param max_stack_depth The new maximum cheax call stack depth.
 *
 * \sa cheax_get_max_stack_depth()
 */
CHX_API void cheax_set_max_stack_depth(CHEAX *c, int max_stack_depth);

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
CHX_API struct chx_value *cheax_read(CHEAX *c, FILE *f);

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
CHX_API struct chx_value *cheax_readstr(CHEAX *c, const char *str);

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
CHX_API struct chx_value *cheax_eval(CHEAX *c, struct chx_value *expr);

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
CHX_API void cheax_print(CHEAX *c, FILE *output, struct chx_value *expr);

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
CHX_API struct chx_value *cheax_format(CHEAX *c, const char *fmt, struct chx_list *args);

/*! \brief Reads a file and executes it.
 *
 * \param c Virtual machine instance.
 * \param f Input file handle.
 */
CHX_API void cheax_exec(CHEAX *c, FILE *f);

/*! @} */

#endif
