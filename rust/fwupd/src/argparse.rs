/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Minimal command-line argument parser.
//!
//! Parses a flat list of strings with a scan-and-consume model: each call to
//! [`Args::option`], [`Args::option_with_value`], or [`Args::positional`] removes the
//! matched token(s) from the internal list.  Call [`Args::finish`] at the end
//! to reject unexpected leftover arguments.
//!
//! # Examples
//!
//! Simple subcommand dispatch:
//!
//! ```
//! use fwupd::argparse::Args;
//!
//! let argv = ["list", "--long", "archive.cab"];
//! let mut args = Args::new(argv);
//!
//! match args.subcommand().as_deref() {
//!     Some("list") => {
//!         let long = args.option(&["-l", "--long"]);
//!         let file = args.positional("FILE").unwrap();
//!         args.finish().unwrap();
//!         assert!(long);
//!         assert_eq!(file, "archive.cab");
//!     }
//!     _ => panic!("unexpected subcommand"),
//! }
//! ```
//!
//! An option that takes a value:
//!
//! ```
//! use fwupd::argparse::Args;
//!
//! let argv = ["--output", "/tmp"];
//! let mut args = Args::new(argv);
//! let output = args.option_with_value(&["-o", "--output"]).unwrap();
//! assert_eq!(output, Some(String::from("/tmp")));
//! ```
//!
//! The `=`-separated form is also accepted:
//!
//! ```
//! use fwupd::argparse::Args;
//!
//! let argv = ["--output=/tmp"];
//! let mut args = Args::new(argv);
//! let output = args.option_with_value(&["-o", "--output"]).unwrap();
//! assert_eq!(output, Some(String::from("/tmp")));
//! ```
//!
//! An option that takes a value, with a default:
//!
//! ```
//! use fwupd::argparse::Args;
//!
//! let argv = ["--blah"];
//! let mut args = Args::new(argv);
//! let output = args.option_with_value_and_default(&["-o", "--output"], "/tmp").unwrap();
//! assert_eq!(output, String::from("/tmp"));
//! ```
//!
//! Collecting variadic positional arguments:
//!
//! ```
//! use fwupd::argparse::Args;
//!
//! let argv = ["out.cab", "a.fw", "b.fw"];
//! let mut args = Args::new(argv);
//! let output = args.positional("OUTPUT").unwrap();
//! let inputs = args.remaining_positionals();
//! assert_eq!(output, "out.cab");
//! assert_eq!(inputs, ["a.fw", "b.fw"]);
//! ```

/// Argument parser built from a list of raw arguments, **excluding `argv[0]`**.
pub struct Args {
    inner: Vec<String>,
}

impl Args {
    /// Create a new parser from an iterator of raw argument strings
    /// starting at `argv[1]`. In most cases this will be `std::env::args().skip(1)`.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["--verbose", "file.cab"];
    /// let mut args = Args::new(argv);
    /// assert!(args.option(&["-v", "--verbose"]));
    /// assert_eq!(args.positional("FILE").unwrap(), "file.cab");
    /// ```
    pub fn new<S: Into<String>>(args: impl IntoIterator<Item = S>) -> Self {
        Self {
            inner: args.into_iter().map(Into::into).collect(),
        }
    }

    /// Returns `true` if `--help` or `-h` appears anywhere in the remaining
    /// arguments.  Does not consume the flag.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["extract", "--help"];
    /// let args = Args::new(argv);
    /// assert!(args.help_requested());
    /// ```
    #[must_use]
    pub fn help_requested(&self) -> bool {
        self.inner.iter().any(|a| a == "--help" || a == "-h")
    }

    /// Remove and return the first argument.
    /// Returns `None` when no arguments are left.
    ///
    /// Can be combined with [`all_remaining_args`](Args::all_remaining_args)
    /// to implement sub-parsers.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["list", "--long", "archive.cab"];
    /// let mut args = Args::new(argv);
    /// assert_eq!(args.subcommand().as_deref(), Some("list"));
    /// // remaining args are available for the subcommand handler
    /// assert!(args.option(&["-l", "--long"]));
    /// ```
    pub fn subcommand(&mut self) -> Option<String> {
        if self.inner.is_empty() {
            None
        } else {
            Some(self.inner.remove(0))
        }
    }

    /// Scan for a boolean flag matching any of `names` (e.g.
    /// `&["-v", "--verbose"]`) and remove it if found.
    /// Only the first occurrence is consumed, so calling this multiple times
    /// can be used to count repeated flags.
    ///
    /// Returns `true` if found, otherwise `false`.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["-l", "archive.cab"];
    /// let mut args = Args::new(argv);
    /// assert!(args.option(&["-l", "--long"]));
    /// assert!(!args.option(&["-l", "--long"])); // already consumed
    /// ```
    pub fn option(&mut self, names: &[&str]) -> bool {
        if let Some(pos) = self.inner.iter().position(|a| names.contains(&a.as_str())) {
            self.inner.remove(pos);
            true
        } else {
            false
        }
    }

    /// Scan for an option matching any of `names` (e.g.
    /// `&["-o", "--output"]`) and consume both the flag and the following value.
    ///
    /// Accepts the value as a separate token (`--output foo.txt`) or joined
    /// with `=` (`--output=foo.txt`).  Only the first occurrence is consumed.
    ///
    /// Returns `Ok(Some(value))` when found, `Ok(None)` when absent.
    ///
    /// # Errors
    ///
    /// Returns an error if the option was found but no value followed it.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// // space-separated
    /// let argv = ["-o", "/tmp"];
    /// let mut args = Args::new(argv);
    /// assert_eq!(args.option_with_value(&["-o", "--output"]).unwrap(), Some("/tmp".to_string()));
    ///
    /// // equals form
    /// let argv = ["--output=/tmp"];
    /// let mut args = Args::new(argv);
    /// assert_eq!(args.option_with_value(&["-o", "--output"]).unwrap(), Some("/tmp".to_string()));
    /// ```
    pub fn option_with_value(&mut self, opts: &[&str]) -> Result<Option<String>, String> {
        if let Some(pos) = self
            .inner
            .iter()
            .position(|arg| opts.contains(&arg.as_str()))
        {
            self.inner.remove(pos);
            if pos < self.inner.len() {
                Ok(Some(self.inner.remove(pos)))
            } else {
                Err(format!("{} requires a value", opts[0]))
            }
        } else {
            // Now check for --output=foo.txt
            let opts_with_equals: Vec<String> = opts.iter().map(|n| format!("{n}=")).collect();
            if let Some(pos) = self
                .inner
                .iter()
                .position(|arg| opts_with_equals.iter().any(|opt| arg.starts_with(opt)))
            {
                let opt = self.inner.remove(pos);
                let value = opt.splitn(2, '=').last();
                if value == Some("") {
                    Err(format!("{} requires a value", opts[0]))
                } else {
                    Ok(value.map(String::from))
                }
            } else {
                Ok(None)
            }
        }
    }

    /// Same as [`option_with_value`](Args::option_with_value) but returns
    /// `default` when the option is absent instead of `None`.
    ///
    /// # Errors
    ///
    /// Returns an error if the option was found but no value followed it.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["archive.cab"];
    /// let mut args = Args::new(argv);
    /// let output = args.option_with_value_and_default(&["-o", "--output"], ".").unwrap();
    /// assert_eq!(output, ".");
    /// ```
    pub fn option_with_value_and_default<S: Into<String>>(
        &mut self,
        opts: &[&str],
        default: S,
    ) -> Result<String, String> {
        self.option_with_value(opts)
            .map(|ok| ok.unwrap_or(default.into()))
    }

    /// Remove and return the first positional (non-flag) argument.
    /// Call multiple times to collect several required positional arguments
    /// in order.
    ///
    /// # Errors
    ///
    /// Returns an error containing `name` if no positional argument remains.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["--long", "archive.cab"];
    /// let mut args = Args::new(argv);
    /// // flags are skipped; the first positional is returned
    /// assert_eq!(args.positional("FILE").unwrap(), "archive.cab");
    /// ```
    pub fn positional(&mut self, name: &str) -> Result<String, String> {
        if let Some(pos) = self.inner.iter().position(|a| !a.starts_with('-')) {
            Ok(self.inner.remove(pos))
        } else {
            Err(format!("missing argument <{name}>"))
        }
    }

    /// Consume `self` and return all remaining arguments as a `Vec<String>`.
    ///
    /// Useful for implementing sub-parsers: peel off the subcommand and any
    /// global flags first, then hand the remainder to the subcommand handler
    /// via a fresh `Args::new(args.all_remaining_args())`.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["--global-flag", "sub", "--sub-flag", "file.cab"];
    /// let mut args = Args::new(argv);
    /// let global = args.option(&["--global-flag"]);
    /// let sub = args.subcommand().unwrap();
    /// // pass the rest to the subcommand handler unchanged
    /// let rest = args.all_remaining_args();
    /// assert!(global);
    /// assert_eq!(sub, "sub");
    /// assert_eq!(rest, ["--sub-flag", "file.cab"]);
    /// ```
    #[must_use]
    pub fn all_remaining_args(self) -> Vec<String> {
        self.inner
    }

    /// Drain and return all remaining positional (non-flag) arguments.
    /// Unrecognized flag-shaped tokens are left in place so that a subsequent
    /// [`finish`](Args::finish) call can report them as unexpected.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["out.cab", "a.fw", "b.fw"];
    /// let mut args = Args::new(argv);
    /// let output = args.positional("OUTPUT").unwrap();
    /// let inputs = args.remaining_positionals();
    /// assert_eq!(output, "out.cab");
    /// assert_eq!(inputs, ["a.fw", "b.fw"]);
    /// assert!(args.finish().is_ok());
    /// ```
    pub fn remaining_positionals(&mut self) -> Vec<String> {
        let out: Vec<String> = self
            .inner
            .iter()
            .filter(|a| !a.starts_with('-'))
            .cloned()
            .collect();
        self.inner.retain(|a| a.starts_with('-'));
        out
    }

    /// Return an error if any arguments remain unconsumed.
    /// Call this after all expected flags and positionals have been parsed to
    /// catch typos or unsupported options.
    ///
    /// # Errors
    ///
    /// Returns an error naming the first unexpected argument.
    ///
    /// # Examples
    ///
    /// ```
    /// use fwupd::argparse::Args;
    ///
    /// let argv = ["--verbose", "file.cab"];
    /// let mut args = Args::new(argv);
    /// args.option(&["-v", "--verbose"]);
    /// args.positional("FILE").unwrap();
    /// assert!(args.finish().is_ok());
    ///
    /// let argv = ["--typo"];
    /// let args = Args::new(argv);
    /// assert!(args.finish().is_err());
    /// ```
    pub fn finish(&self) -> Result<(), String> {
        match self.inner.first() {
            Some(arg) => Err(format!("unexpected argument: {arg}")),
            None => Ok(()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Args;

    fn args(v: &[&str]) -> Args {
        Args::new(v.iter().copied())
    }

    // ── new / empty ──────────────────────────────────────────────────────────

    #[test]
    fn new_empty() {
        let mut a = args(&[]);
        assert!(a.subcommand().is_none());
        assert!(a.finish().is_ok());
    }

    // ── subcommand ───────────────────────────────────────────────────────────

    #[test]
    fn subcommand_returns_first_arg() {
        let mut a = args(&["list", "file.cab"]);
        assert_eq!(a.subcommand().as_deref(), Some("list"));
    }

    #[test]
    fn subcommand_consumes_only_first() {
        let mut a = args(&["list", "file.cab"]);
        a.subcommand();
        assert_eq!(a.positional("FILE").unwrap(), "file.cab");
        assert!(a.finish().is_ok());
    }

    #[test]
    fn subcommand_none_on_empty() {
        let mut a = args(&[]);
        assert!(a.subcommand().is_none());
    }

    // ── help_requested ───────────────────────────────────────────────────────

    #[test]
    fn help_requested_long() {
        assert!(args(&["--help"]).help_requested());
    }

    #[test]
    fn help_requested_short() {
        assert!(args(&["-h"]).help_requested());
    }

    #[test]
    fn help_requested_anywhere() {
        assert!(args(&["list", "file.cab", "--help"]).help_requested());
    }

    #[test]
    fn help_requested_false_when_absent() {
        assert!(!args(&["list", "file.cab"]).help_requested());
    }

    #[test]
    fn help_requested_does_not_consume() {
        let a = args(&["--help"]);
        assert!(a.help_requested());
        assert!(a.help_requested()); // still there
        assert!(a.finish().is_err()); // still unconsumed
    }

    // ── option (boolean flag) ────────────────────────────────────────────────

    #[test]
    fn option_long_form() {
        let mut a = args(&["--long"]);
        assert!(a.option(&["-l", "--long"]));
        assert!(a.finish().is_ok());
    }

    #[test]
    fn option_short_form() {
        let mut a = args(&["-l"]);
        assert!(a.option(&["-l", "--long"]));
        assert!(a.finish().is_ok());
    }

    #[test]
    fn option_absent_returns_false() {
        let mut a = args(&["file.cab"]);
        assert!(!a.option(&["-l", "--long"]));
    }

    #[test]
    fn option_consumes_only_first_occurrence() {
        let mut a = args(&["--long", "--long"]);
        assert!(a.option(&["-l", "--long"])); // consumes one
        assert!(a.option(&["-l", "--long"])); // consumes the other
        assert!(!a.option(&["-l", "--long"])); // none left
        assert!(a.finish().is_ok());
    }

    #[test]
    fn option_mixed_with_positionals() {
        let mut a = args(&["file.cab", "--long", "other.cab"]);
        assert!(a.option(&["-l", "--long"]));
        assert_eq!(a.positional("A").unwrap(), "file.cab");
        assert_eq!(a.positional("B").unwrap(), "other.cab");
        assert!(a.finish().is_ok());
    }

    // ── option_with_value ────────────────────────────────────────────────────

    #[test]
    fn option_with_value_space_separated() {
        let mut a = args(&["--output", "/tmp"]);
        assert_eq!(
            a.option_with_value(&["-o", "--output"]).unwrap(),
            Some("/tmp".to_string())
        );
        assert!(a.finish().is_ok());
    }

    #[test]
    fn option_with_value_short_form() {
        let mut a = args(&["-o", "/tmp"]);
        assert_eq!(
            a.option_with_value(&["-o", "--output"]).unwrap(),
            Some("/tmp".to_string())
        );
        assert!(a.finish().is_ok());
    }

    #[test]
    fn option_with_value_equals_form() {
        let mut a = args(&["--output=/tmp"]);
        assert_eq!(
            a.option_with_value(&["-o", "--output"]).unwrap(),
            Some("/tmp".to_string())
        );
        assert!(a.finish().is_ok());
    }

    #[test]
    fn option_with_value_absent_returns_none() {
        let mut a = args(&["file.cab"]);
        assert_eq!(a.option_with_value(&["-o", "--output"]).unwrap(), None);
    }

    #[test]
    fn option_with_value_missing_value_is_err() {
        let mut a = args(&["--output"]);
        assert!(a.option_with_value(&["-o", "--output"]).is_err());
    }

    #[test]
    fn option_with_value_consumes_only_first() {
        let mut a = args(&["--output", "/tmp", "--output", "/var"]);
        assert_eq!(
            a.option_with_value(&["-o", "--output"]).unwrap(),
            Some("/tmp".to_string())
        );
        assert_eq!(
            a.option_with_value(&["-o", "--output"]).unwrap(),
            Some("/var".to_string())
        );
        assert!(a.finish().is_ok());
    }

    // ── positional ───────────────────────────────────────────────────────────

    #[test]
    fn positional_returns_first_non_flag() {
        let mut a = args(&["--long", "file.cab"]);
        assert_eq!(a.positional("FILE").unwrap(), "file.cab");
    }

    #[test]
    fn positional_multiple_calls_in_order() {
        let mut a = args(&["out.cab", "a.fw", "b.fw"]);
        assert_eq!(a.positional("OUTPUT").unwrap(), "out.cab");
        assert_eq!(a.positional("A").unwrap(), "a.fw");
        assert_eq!(a.positional("B").unwrap(), "b.fw");
        assert!(a.finish().is_ok());
    }

    #[test]
    fn positional_missing_is_err() {
        let mut a = args(&["--long"]);
        let err = a.positional("FILE").unwrap_err();
        assert!(
            err.contains("FILE"),
            "error message should name the argument"
        );
    }

    #[test]
    fn positional_on_empty_is_err() {
        let mut a = args(&[]);
        assert!(a.positional("FILE").is_err());
    }

    // ── remaining_positionals ────────────────────────────────────────────────

    #[test]
    fn remaining_positionals_drains_non_flags() {
        let mut a = args(&["a.fw", "--flag", "b.fw"]);
        let rest = a.remaining_positionals();
        assert_eq!(rest, ["a.fw", "b.fw"]);
        // the unconsumed flag stays so finish() catches it
        assert!(a.finish().is_err());
    }

    #[test]
    fn remaining_positionals_empty_when_none() {
        let mut a = args(&["--flag"]);
        assert_eq!(a.remaining_positionals(), Vec::<String>::new());
    }

    #[test]
    fn remaining_positionals_after_positional() {
        let mut a = args(&["out.cab", "a.fw", "b.fw"]);
        let output = a.positional("OUTPUT").unwrap();
        let inputs = a.remaining_positionals();
        assert_eq!(output, "out.cab");
        assert_eq!(inputs, ["a.fw", "b.fw"]);
        assert!(a.finish().is_ok());
    }

    // ── all_remaining_args ───────────────────────────────────────────────────

    #[test]
    fn all_remaining_args_returns_everything() {
        let mut a = args(&["sub", "--flag", "pos"]);
        a.subcommand();
        assert_eq!(a.all_remaining_args(), ["--flag", "pos"]);
    }

    #[test]
    fn all_remaining_args_empty() {
        let a = args(&[]);
        assert_eq!(a.all_remaining_args(), Vec::<String>::new());
    }

    // ── finish ───────────────────────────────────────────────────────────────

    #[test]
    fn finish_ok_when_empty() {
        assert!(args(&[]).finish().is_ok());
    }

    #[test]
    fn finish_err_names_leftover_arg() {
        let a = args(&["--unknown"]);
        let err = a.finish().unwrap_err();
        assert!(err.contains("--unknown"));
    }

    #[test]
    fn finish_err_on_leftover_positional() {
        let a = args(&["leftover"]);
        assert!(a.finish().is_err());
    }
}
