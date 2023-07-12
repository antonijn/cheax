cheax
=====

[![Build status](https://github.com/antonijn/cheax/actions/workflows/build-test.yml/badge.svg)](https://github.com/antonijn/cheax/actions)

A Lisp dialect that looks a bit like Haskell. Designed for easy
interopability with C/C++.

Installation
------------

```sh
$ mkdir build && cd build && cmake .. && make
# make install
```

Language demo
-------------
![Made with VHS](https://vhs.charm.sh/vhs-4S0HmQsFAYuSxQuZ6i1HJE.gif)

C API example
-------------

```C
#include <cheax.h>

#include <stdio.h>

int main(void)
{
	int result;
	CHEAX *c = cheax_init();

	/* Load some "unsafe" functions, e.g. file io and the program
	 * exit function */
	cheax_load_feature(c, "all");

	/* Make sure cheax doesn't cause a stack overflow */
	cheax_config_int(c, "stack-limit", 4096);

	/* Load the standard library */
	if (cheax_load_prelude(c) < 0) {
		/* Display error message on behalf of "example" program */
		cheax_perror(c, "example");
		return EXIT_FAILURE;
	}

	/* Synchronize variable `result' with the eponymous symbol
	 * in cheax */
	cheax_sync_int(c, "result", &result, 0);

	cheax_eval(c, cheax_readstr(c, "(set result (sum (.. 1 100)))"));

	cheax_destroy(c);

	printf("1 + 2 + ... + 100 = %d\n", result);
	return 0;
}
```
