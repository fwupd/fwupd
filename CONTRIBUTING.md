# Contributor Guidelines

## Getting started

To set up your local environment, from the top level of the checkout run

```shell
./contrib/setup
```

This will create pre-commit hooks to fixup many code style issues before your
code is submitted.

On some Linux distributions this will install all build dependencies needed
to compile fwupd as well.

## Coding Style

The coding style to respect in this project is very similar to most
GLib projects. In particular, the following rules are largely adapted
from the PackageKit Coding Style.

* 8-space tabs for indentation

* Prefer lines of less than <= 100 columns

* No spaces between function name and braces (both calls and macro
   declarations)

* If function signature/call fits in a single line, do not break it
   into multiple lines

* Prefer descriptive names over abbreviations (unless well-known)
   and shortening of names. e.g `device` not `dev`

* Single statements inside if/else should not be enclosed by '{}'

* Use comments to explain why something is being done, but also avoid
   over-documenting the obvious. Here is an example of useless comment:

   // Fetch the document
   fetch_the_document();

* Comments should not start with a capital letter or end with a full stop and
   should be C-style, not C++-style, e.g. `/* this */` not `// this`

* Each object should go in a separate .c file and be named according
   to the class

* Use g_autoptr() and g_autofree whenever possible, and avoid `goto out`
   error handling

* Failing methods should return FALSE with a suitable `GError` set

* Trailing whitespace is forbidden

* Pointers should be checked for NULL explicitly, e.g. `foo != NULL` not `!foo`

* Use the correct debug level:

  * `g_debug()` -- low level plugin and daemon development, typically only useful to programmers
  * `g_info()` -- generally useful messages, typically shown when using `--verbose`
  * `g_message()` -- important messages, typically shown in service output
  * `g_warning()` -- warning messages, typically shown in service output
  * `g_critical()` -- critical messages, typically shown before the daemon aborts

**NOTE:** Do not use `g_error()` -- it's not appropriate to abort the daemon on error.

`./contrib/reformat-code.py` can be used in order to get automated
formatting. Calling the script without arguments formats the current
patch while passing commits will do formatting on everything changed since that
commit.
