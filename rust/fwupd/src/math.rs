/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Math helpers

use core::fmt;
use core::num::{
    NonZeroI128, NonZeroI16, NonZeroI32, NonZeroI64, NonZeroI8, NonZeroIsize, NonZeroU128,
    NonZeroU16, NonZeroU32, NonZeroU64, NonZeroU8, NonZeroUsize,
};

use crate::SafeUnwrap;

/// Error returned by [`OverflowSafeOps`] functions to indicate
/// that a mathematical operation failed.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum OverflowError<T> {
    /// Overflow happened during `a + b`
    Add { a: T, b: T },
    /// Underflow happened during `a - b`
    Sub { a: T, b: T },
    /// Overflow happened during `a * b`
    Mul { a: T, b: T },
    /// Overflow happened during `a / b`
    Div { a: T, b: T },
}

impl<T: std::fmt::Display> std::fmt::Display for OverflowError<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Add { a, b } => write!(f, "overflow in addition: {a} + {b}"),
            Self::Sub { a, b } => write!(f, "underflow in subtraction: {a} - {b}"),
            Self::Mul { a, b } => write!(f, "overflow in multiplication: {a} * {b}"),
            Self::Div { a, b } => write!(f, "overflow in division: {a} / {b}"),
        }
    }
}

impl<T: fmt::Debug + fmt::Display> std::error::Error for OverflowError<T> {}

/// Convenience trait for overflow-safe operations, equivalent to the stdlib's
/// [`usize::checked_add`], [`usize::overflowing_add`], etc.
///
/// The functions here return an [`OverflowError`], which simplifies
/// callers - they no longer have to convert every single `None` from
/// `checked_add()` to the right error value. This simplifies chaining and
/// error reporting.
///
/// # Example
///
/// ```
/// use fwupd::math::{OverflowSafeOps, OverflowError};
///
/// fn read_from_wire(offset: usize) -> Result<(), OverflowError<usize>> {
///     let struct_size = 0xffffusize;
///     // read struct from wire
///     offset.safe_add(struct_size)?;
///
///     let other_struct_size = 0xffffusize;
///     // read struct from wire
///     offset.safe_add(other_struct_size)?;
///
///     Ok(())
/// }
/// ```
pub trait OverflowSafeOps: Sized {
    /// The non-zero counterpart of `Self`, e.g. [`std::num::NonZeroUsize`]
    /// for `usize`.
    // Separate `type` needed because the generic [`std::num::NonZero<T>`] requires
    // the unstable `ZeroablePrimitive` bound, so the non-zero type must
    // be named per implementation.
    type NonZero;

    /// Add `value` to self.
    ///
    /// # Errors
    ///
    /// Returns [`OverflowError::Add`] if the addition overflows.
    fn safe_add(self, value: Self) -> Result<Self, OverflowError<Self>>;

    /// Subtract `value` from self.
    ///
    /// # Errors
    ///
    /// Returns [`OverflowError::Sub`] if the subtraction underflows.
    fn safe_sub(self, value: Self) -> Result<Self, OverflowError<Self>>;

    /// Multiply self by `value`.
    ///
    /// # Errors
    ///
    /// Returns [`OverflowError::Mul`] if the multiplication overflows.
    fn safe_mul(self, value: Self) -> Result<Self, OverflowError<Self>>;

    /// Divide self by `value`.
    ///
    /// # Errors
    ///
    /// Returns [`OverflowError::Div`] if the division overflows.
    ///
    /// For unsigned types like `usize` this cannot fail: the divisor is
    /// guaranteed non-zero by the [`NonZero`](Self::NonZero) type and
    /// unsigned division never overflows. For signed types it fails only
    /// on `MIN / -1`, which overflows.
    fn safe_div(self, value: Self::NonZero) -> Result<Self, OverflowError<Self>>;
}

macro_rules! impl_safe_math {
    ($(($t:ty, $nz:ty)),* $(,)?) => {
        $(
            impl OverflowSafeOps for $t {
                type NonZero = $nz;

                fn safe_add(self, value: $t) -> Result<$t, OverflowError<$t>> {
                    self.checked_add(value)
                        .ok_or(OverflowError::Add { a: self, b: value })
                }

                fn safe_sub(self, value: $t) -> Result<$t, OverflowError<$t>> {
                    self.checked_sub(value)
                        .ok_or(OverflowError::Sub { a: self, b: value })
                }

                fn safe_mul(self, value: $t) -> Result<$t, OverflowError<$t>> {
                    self.checked_mul(value)
                        .ok_or(OverflowError::Mul { a: self, b: value })
                }

                fn safe_div(self, value: $nz) -> Result<$t, OverflowError<$t>> {
                    self.checked_div(value.get())
                        .ok_or(OverflowError::Div { a: self, b: value.get() })
                }
            }
        )*
    };
}

impl_safe_math![
    (u8, NonZeroU8),
    (u16, NonZeroU16),
    (u32, NonZeroU32),
    (u64, NonZeroU64),
    (u128, NonZeroU128),
    (usize, NonZeroUsize)
];
impl_safe_math![
    (i8, NonZeroI8),
    (i16, NonZeroI16),
    (i32, NonZeroI32),
    (i64, NonZeroI64),
    (i128, NonZeroI128),
    (isize, NonZeroIsize)
];

/// Trait linking a non-zero primitive type (e.g. [`NonZeroUsize`]) to
/// its zeroable base type (e.g. `usize`).
///
// This is needed because the generic [`std::num::NonZero<T>`] requires
// the unstable `ZeroablePrimitive` bound, so generic code cannot call
// `new()` or `get()` on `T::NonZero` directly.
pub trait OverflowSafeNonZero: Sized {
    /// The zeroable base type, e.g. `usize` for [`NonZeroUsize`].
    type Base: OverflowSafeOps<NonZero = Self>;

    /// Create from the base type, returns `None` if `value` is zero.
    fn new(value: Self::Base) -> Option<Self>;

    /// Return the underlying base type value.
    fn get(self) -> Self::Base;
}

macro_rules! impl_safe_non_zero {
    ($(($t:ty, $nz:ty)),* $(,)?) => {
        $(
            impl OverflowSafeNonZero for $nz {
                type Base = $t;

                fn new(value: $t) -> Option<Self> {
                    <$nz>::new(value)
                }

                fn get(self) -> $t {
                    self.get()
                }
            }
        )*
    };
}

impl_safe_non_zero![
    (u8, NonZeroU8),
    (u16, NonZeroU16),
    (u32, NonZeroU32),
    (u64, NonZeroU64),
    (u128, NonZeroU128),
    (usize, NonZeroUsize)
];
impl_safe_non_zero![
    (i8, NonZeroI8),
    (i16, NonZeroI16),
    (i32, NonZeroI32),
    (i64, NonZeroI64),
    (i128, NonZeroI128),
    (isize, NonZeroIsize)
];

/// Newtype wrapper around a number type T that
/// provides overflow-safe operations, see [`OverflowSafeOps`].
///
/// [`a + b`](std::ops::Add), [`a - b`](std::ops::Sub), [`a * b`](std::ops::Mul), and
/// [`a / b`](std::ops::Div) are implemented for the various
/// primitive types, allowing for math that cannot overflow
/// without being caught at compile time.
///
/// [`a += b`](std::ops::AddAssign), [`a -= b`](std::ops::SubAssign),
/// [`a *= b`](std::ops::MulAssign), and [`a /= b`](std::ops::DivAssign)
/// are **not** implemented (the respective Rust traits cannot fail).
/// Instead:
/// - for `a += b` use [`a.add(b)`](Self::add)
/// - for `a -= b` use [`a.sub(b)`](Self::sub)
/// - for `a *= b` use [`a.mul(b)`](Self::mul)
/// - for `a /= b` use [`a.div(b)`](Self::div)
///
/// # Example
///
/// ```
/// use fwupd::math::OverflowSafe;
///
/// # fn example() {
/// let mut b = OverflowSafe::from(usize::MAX);
/// let c = (b + 1).expect("whoops, overflow");
///
/// // Compiler error, AddAssign does not exist!
/// // b += 1;
///
/// // Use add/sub/mul/div instead
/// b.sub(1).unwrap();
/// assert_eq!(b.get(), usize::MAX - 1);
/// # }
/// ```
///
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct OverflowSafe<T: OverflowSafeOps>(T);

impl<T: OverflowSafeOps> OverflowSafe<T> {
    pub fn get(self) -> T {
        self.0
    }
}

impl<T: Copy + OverflowSafeOps> OverflowSafe<T> {
    /// Add `value` to self. This is the equivalent to `self += value`.
    ///
    /// # Errors
    ///
    /// Returns an [`OverflowError`] on error.
    pub fn add(&mut self, value: T) -> Result<(), OverflowError<T>> {
        self.0 = self.0.safe_add(value)?;
        Ok(())
    }

    /// Subtract `value` to self. This is the equivalent to `self -= value`.
    ///
    /// # Errors
    ///
    /// Returns an [`OverflowError`] on error.
    pub fn sub(&mut self, value: T) -> Result<(), OverflowError<T>> {
        self.0 = self.0.safe_sub(value)?;
        Ok(())
    }

    /// Multiply `value` with self. This is the equivalent to `self *= value`.
    ///
    /// # Errors
    ///
    /// Returns an [`OverflowError`] on error.
    pub fn mul(&mut self, value: T) -> Result<(), OverflowError<T>> {
        self.0 = self.0.safe_mul(value)?;
        Ok(())
    }

    /// Divide self by `value`. This is the equivalent to `self /= value`.
    ///
    /// # Errors
    ///
    /// Returns an [`OverflowError`] on error.
    pub fn div(&mut self, value: T::NonZero) -> Result<(), OverflowError<T>> {
        self.0 = self.0.safe_div(value)?;
        Ok(())
    }
}

impl From<OverflowSafe<usize>> for usize {
    fn from(value: OverflowSafe<usize>) -> Self {
        value.get()
    }
}

impl<T: OverflowSafeOps> From<T> for OverflowSafe<T> {
    fn from(value: T) -> Self {
        Self(value)
    }
}

/// Newtype wrapper around a non-zero number type, the counterpart of
/// [`OverflowSafe`] for values that are guaranteed to be non-zero.
///
/// # Example
///
/// ```
/// use fwupd::math::{NonZeroOverflowSafe, OverflowSafe};
///
/// # fn example() {
/// let divisor: NonZeroOverflowSafe<usize> = NonZeroOverflowSafe::new(3).expect("non-zero");
/// let dividend: OverflowSafe<usize> = OverflowSafe::from(6);
/// let quotient = (dividend / divisor).expect("whoops, overflow");
/// assert_eq!(quotient.get(), 2);
/// # }
/// ```
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct NonZeroOverflowSafe<T: OverflowSafeOps>(T::NonZero)
where
    T::NonZero: OverflowSafeNonZero<Base = T>;

impl<T: OverflowSafeOps> NonZeroOverflowSafe<T>
where
    T::NonZero: OverflowSafeNonZero<Base = T>,
{
    /// Create a [`NonZeroOverflowSafe`] from the base type, returns `None` if
    /// `value` is zero.
    pub fn new(value: T) -> Option<Self> {
        T::NonZero::new(value).map(Self)
    }

    /// Return the underlying value, guaranteed to be non-zero.
    pub fn get(self) -> T {
        self.0.get()
    }

    /// Wrap an existing non-zero primitive, e.g. a [`NonZeroUsize`].
    ///
    /// This is an inherent method rather than a `From` impl because the
    /// generic `impl<T: OverflowSafeOps> From<T::NonZero> for NonZeroOverflowSafe<T>`
    /// would conflict with the reflexive `impl<T> From<T> for T` in `core`.
    pub fn from_nonzero(value: T::NonZero) -> Self {
        Self(value)
    }
}

impl<T: OverflowSafeOps> OverflowSafe<T>
where
    T::NonZero: OverflowSafeNonZero<Base = T>,
{
    /// Convert into a [`NonZeroOverflowSafe`], returns `None` if the value is zero.
    pub fn into_nonzero(self) -> Option<NonZeroOverflowSafe<T>> {
        NonZeroOverflowSafe::new(self.0)
    }
}

impl<T: OverflowSafeOps> std::ops::Add<OverflowSafe<T>> for OverflowSafe<T> {
    type Output = Result<Self, OverflowError<T>>;

    fn add(self, rhs: OverflowSafe<T>) -> Self::Output {
        self.0.safe_add(rhs.0).map(OverflowSafe)
    }
}

impl<T: OverflowSafeOps> std::ops::Add<T> for OverflowSafe<T> {
    type Output = Result<Self, OverflowError<T>>;

    fn add(self, rhs: T) -> Self::Output {
        self.0.safe_add(rhs).map(OverflowSafe)
    }
}

impl<T: OverflowSafeOps> std::ops::Sub<OverflowSafe<T>> for OverflowSafe<T> {
    type Output = Result<Self, OverflowError<T>>;

    fn sub(self, rhs: OverflowSafe<T>) -> Self::Output {
        self.0.safe_sub(rhs.0).map(OverflowSafe)
    }
}

impl<T: OverflowSafeOps> std::ops::Sub<T> for OverflowSafe<T> {
    type Output = Result<Self, OverflowError<T>>;

    fn sub(self, rhs: T) -> Self::Output {
        self.0.safe_sub(rhs).map(OverflowSafe)
    }
}

impl<T: OverflowSafeOps> std::ops::Mul<OverflowSafe<T>> for OverflowSafe<T> {
    type Output = Result<Self, OverflowError<T>>;

    fn mul(self, rhs: OverflowSafe<T>) -> Self::Output {
        self.0.safe_mul(rhs.0).map(OverflowSafe)
    }
}

impl<T: OverflowSafeOps> std::ops::Mul<T> for OverflowSafe<T> {
    type Output = Result<Self, OverflowError<T>>;

    fn mul(self, rhs: T) -> Self::Output {
        self.0.safe_mul(rhs).map(OverflowSafe)
    }
}

/// Dividing a [`OverflowSafe`] by a [`NonZeroOverflowSafe`] cannot divide by zero;
/// the only possible failure is `MIN / -1` for signed types.
impl<T: OverflowSafeOps> std::ops::Div<NonZeroOverflowSafe<T>> for OverflowSafe<T>
where
    T::NonZero: OverflowSafeNonZero<Base = T>,
{
    type Output = Result<Self, OverflowError<T>>;

    fn div(self, rhs: NonZeroOverflowSafe<T>) -> Self::Output {
        self.0.safe_div(rhs.0).map(OverflowSafe)
    }
}

/// Multiplying two non-zero numbers yields a non-zero number, so the
/// [`NonZeroOverflowSafe`] type is preserved. It can still overflow, though.
impl<T: OverflowSafeOps> std::ops::Mul<NonZeroOverflowSafe<T>> for NonZeroOverflowSafe<T>
where
    T::NonZero: OverflowSafeNonZero<Base = T>,
{
    type Output = Result<Self, OverflowError<T>>;

    fn mul(self, rhs: NonZeroOverflowSafe<T>) -> Self::Output {
        let result = self.0.get().safe_mul(rhs.0.get())?;
        // the product of two non-zero numbers cannot be zero
        Ok(NonZeroOverflowSafe::new(result).safe_to_unwrap())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add() {
        assert_eq!(1u32.safe_add(2), Ok(3));
        assert_eq!(
            u32::MAX.safe_add(1),
            Err(OverflowError::Add { a: u32::MAX, b: 1 })
        );
    }

    #[test]
    fn sub() {
        assert_eq!(3u32.safe_sub(1), Ok(2));
        assert_eq!(0u32.safe_sub(1), Err(OverflowError::Sub { a: 0, b: 1 }));
    }

    #[test]
    fn mul() {
        assert_eq!(2u32.safe_mul(3), Ok(6));
        assert_eq!(
            u32::MAX.safe_mul(2),
            Err(OverflowError::Mul { a: u32::MAX, b: 2 })
        );
    }

    #[test]
    fn div_unsigned() {
        let two = NonZeroU32::new(2).unwrap();
        assert_eq!(6u32.safe_div(two), Ok(3));
        // unsigned division can never fail
        assert_eq!(u32::MAX.safe_div(two), Ok(u32::MAX / 2));
    }

    #[test]
    fn div_signed_min_over_neg_one() {
        let minus_one = NonZeroI8::new(-1).unwrap();
        assert_eq!(
            i8::MIN.safe_div(minus_one),
            Err(OverflowError::Div { a: i8::MIN, b: -1 })
        );
        // every other signed division succeeds
        assert_eq!((-128i8).safe_div(NonZeroI8::new(-2).unwrap()), Ok(64));
    }

    #[test]
    fn chaining() -> Result<(), OverflowError<usize>> {
        let offset = 0x10usize;
        let len = 0x20usize;
        assert_eq!(offset.safe_add(len)?.safe_mul(2)?, 0x60);
        Ok(())
    }

    #[test]
    fn display() {
        let err = u8::MAX.safe_add(1).unwrap_err();
        assert_eq!(err.to_string(), "overflow in addition: 255 + 1");
    }

    #[test]
    fn nonzero_safe_num() {
        assert!(NonZeroOverflowSafe::<usize>::new(0).is_none());
        let two = NonZeroOverflowSafe::<usize>::new(2).unwrap();
        assert_eq!(two.get(), 2);

        let six = OverflowSafe::from(6usize);
        let quotient = (six / two).unwrap();
        assert_eq!(quotient.get(), 3);

        let four = NonZeroOverflowSafe::<usize>::new(4).unwrap();
        let product = (two * four).unwrap();
        assert_eq!(product.get(), 8);
    }

    #[test]
    fn nonzero_safe_num_conversions() {
        let nz = NonZeroU32::new(42).unwrap();
        let value = NonZeroOverflowSafe::<u32>::from_nonzero(nz);
        assert_eq!(value.get(), 42);

        let zero = OverflowSafe::from(0u32);
        assert!(zero.into_nonzero().is_none());
        let nonzero = OverflowSafe::from(1u32);
        assert_eq!(nonzero.into_nonzero().unwrap().get(), 1);
    }

    #[test]
    fn nonzero_safe_num_mul_overflow() {
        let big = NonZeroOverflowSafe::<u32>::new(u32::MAX).unwrap();
        let two = NonZeroOverflowSafe::<u32>::new(2).unwrap();
        assert_eq!(big * two, Err(OverflowError::Mul { a: u32::MAX, b: 2 }));
    }

    #[test]
    fn nonzero_safe_num_div_min_over_neg_one() {
        let minus_one = NonZeroOverflowSafe::<i8>::new(-1).unwrap();
        let min = OverflowSafe::from(i8::MIN);
        assert_eq!(
            min / minus_one,
            Err(OverflowError::Div { a: i8::MIN, b: -1 })
        );
    }

    /// Check the macro-generated `OverflowSafeOps` impl for one primitive
    /// type: success and overflow paths of add/sub/mul, plus div.
    fn check_primitives<T>(min: T, max: T, one: T, two: T, three: T, six: T)
    where
        T: OverflowSafeOps + Copy + PartialEq + std::fmt::Debug,
        T::NonZero: OverflowSafeNonZero<Base = T>,
    {
        assert_eq!(one.safe_add(two), Ok(three));
        assert_eq!(three.safe_sub(two), Ok(one));
        assert_eq!(two.safe_mul(three), Ok(six));

        assert_eq!(
            max.safe_add(one),
            Err(OverflowError::Add { a: max, b: one })
        );
        assert_eq!(
            min.safe_sub(one),
            Err(OverflowError::Sub { a: min, b: one })
        );
        assert_eq!(
            max.safe_mul(two),
            Err(OverflowError::Mul { a: max, b: two })
        );

        let nz_two = <T::NonZero as OverflowSafeNonZero>::new(two).unwrap();
        assert_eq!(six.safe_div(nz_two), Ok(three));
    }

    #[test]
    fn primitives_unsigned() {
        check_primitives(u8::MIN, u8::MAX, 1, 2, 3, 6);
        check_primitives(u16::MIN, u16::MAX, 1, 2, 3, 6);
        check_primitives(u32::MIN, u32::MAX, 1, 2, 3, 6);
        check_primitives(u64::MIN, u64::MAX, 1, 2, 3, 6);
        check_primitives(u128::MIN, u128::MAX, 1, 2, 3, 6);
        check_primitives(usize::MIN, usize::MAX, 1, 2, 3, 6);
    }

    #[test]
    fn primitives_signed() {
        check_primitives(i8::MIN, i8::MAX, 1, 2, 3, 6);
        check_primitives(i16::MIN, i16::MAX, 1, 2, 3, 6);
        check_primitives(i32::MIN, i32::MAX, 1, 2, 3, 6);
        check_primitives(i64::MIN, i64::MAX, 1, 2, 3, 6);
        check_primitives(i128::MIN, i128::MAX, 1, 2, 3, 6);
        check_primitives(isize::MIN, isize::MAX, 1, 2, 3, 6);
    }

    #[test]
    fn overflow_safe_ops() {
        let a = OverflowSafe::from(6usize);
        let b = OverflowSafe::from(3usize);

        // RHS is OverflowSafe<T>
        assert_eq!((a + b).unwrap().get(), 9);
        assert_eq!((a - b).unwrap().get(), 3);
        assert_eq!((a * b).unwrap().get(), 18);

        // RHS is the bare primitive
        assert_eq!((a + 3).unwrap().get(), 9);
        assert_eq!((a - 3).unwrap().get(), 3);
        assert_eq!((a * 3).unwrap().get(), 18);

        // overflow paths
        let max = OverflowSafe::from(usize::MAX);
        assert_eq!(
            max + 1,
            Err(OverflowError::Add {
                a: usize::MAX,
                b: 1
            })
        );
        let zero = OverflowSafe::from(0usize);
        assert_eq!(zero - 1, Err(OverflowError::Sub { a: 0, b: 1 }));
        assert_eq!(
            max * 2,
            Err(OverflowError::Mul {
                a: usize::MAX,
                b: 2
            })
        );
    }

    #[test]
    fn overflow_safe_mutating() {
        let mut v = OverflowSafe::from(10usize);
        v.add(5).unwrap();
        assert_eq!(v.get(), 15);
        v.sub(5).unwrap();
        assert_eq!(v.get(), 10);
        v.mul(2).unwrap();
        assert_eq!(v.get(), 20);
        v.div(NonZeroUsize::new(4).unwrap()).unwrap();
        assert_eq!(v.get(), 5);

        // error paths leave the value untouched
        let mut max = OverflowSafe::from(usize::MAX);
        assert_eq!(
            max.add(1),
            Err(OverflowError::Add {
                a: usize::MAX,
                b: 1
            })
        );
        assert_eq!(max.get(), usize::MAX);
        let mut zero = OverflowSafe::from(0usize);
        assert_eq!(zero.sub(1), Err(OverflowError::Sub { a: 0, b: 1 }));
        assert_eq!(zero.get(), 0);
        let mut min = OverflowSafe::from(i8::MIN);
        assert_eq!(
            min.div(NonZeroI8::new(-1).unwrap()),
            Err(OverflowError::Div { a: i8::MIN, b: -1 })
        );
        assert_eq!(min.get(), i8::MIN);
    }

    #[test]
    fn overflow_safe_conversions() {
        let v = OverflowSafe::from(42usize);
        assert_eq!(usize::from(v), 42);
        assert_eq!(v.get(), 42);

        // generic From<T>
        let w: OverflowSafe<u8> = 7u8.into();
        assert_eq!(w.get(), 7);
    }

    #[test]
    fn error_display_variants() {
        let sub = u8::MIN.safe_sub(1).unwrap_err();
        assert_eq!(sub.to_string(), "underflow in subtraction: 0 - 1");
        let mul = u8::MAX.safe_mul(2).unwrap_err();
        assert_eq!(mul.to_string(), "overflow in multiplication: 255 * 2");
        let div_err = i8::MIN.safe_div(NonZeroI8::new(-1).unwrap()).unwrap_err();
        assert_eq!(div_err.to_string(), "overflow in division: -128 / -1");
    }

    #[test]
    fn error_is_std_error() {
        let err: Box<dyn std::error::Error> = Box::new(u8::MAX.safe_add(1).unwrap_err());
        assert_eq!(err.to_string(), "overflow in addition: 255 + 1");
        assert!(err.source().is_none());
    }
}
