cheax
=====

A Lisp dialect that looks a bit like Haskell. Designed for easy
interopability with C/C++.

Installation
------------

```sh
$ cmake .
$ make
# make install
```

Language example
----------------

```
> (print "Hello, world!")
Hello, world!

> (print '(1 2 3))          ; ' allows you to create symbols without evaluation
(1 2 3)

> (print (: 4 ('5 6)))      ; (:) is the list-append operator
(4 5 6)

> (defun sum (lst)
…  (case lst                     ; matches lst with any of the following cases
…    ((: x xs) (+ x (sum xs)))   ; non-empty list with head x, tail xs
…    (()       0)))              ; empty list

> (sum (.. 1 100))          ; (..) is a built-in function generating a list of numbers
5050
```

C API example
-------------

```C
#include <cheax.h>

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	int result;
	CHEAX *c = cheax_init();

	/* Loads some "unsafe" functions, i.e. setting the maximum stack depth,
	   IO functions etc. */
	cheax_load_extra_builtins(c, CHEAX_ALL_BUILTINS);

	/* Load the standard library */
	if (cheax_load_prelude(c)) {
		perror("failed to load prelude");
		return EXIT_FAILURE;
	}

	cheax_sync(c, "result", CHEAX_INT, &result);

	cheax_eval(c, cheax_readstr(c, "(set result (sum (.. 1 100)))"));

	cheax_destroy(c);

	printf("1 + 2 + ... + 100 = %d\n", result);
	return 0;
}
```
