/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Slice helpers

/// A struct representing a slice with at least one element.
///
/// ```
/// use fwupd::slice::NonEmptySlice;
///
/// fn process(data: &NonEmptySlice<u8>) -> u8 {
///     // No need to handle the empty case — it's unrepresentable.
///     data.first() + data.len() as u8
/// }
///
/// fn main() {
///     let bytes = [1u8, 2, 3];
///     match NonEmptySlice::new(&bytes) {
///         Some(ne) => println!("first: {}, len: {}", process(ne), ne.len()),
///         None => println!("empty input"),
///     }
///
///     // Doesn't compile: None is not &NonEmptySlice<u8>
///     // process(NonEmptySlice::new(&[]).unwrap_or_default());
/// }
/// ```
#[repr(transparent)]
pub struct NonEmptySlice<T>([T]);

impl<T> NonEmptySlice<T> {
    pub fn new(s: &[T]) -> Option<&Self> {
        if s.is_empty() {
            None
        } else {
            // Safety: NonEmptySlice is repr(transparent) so
            // &NonEmptySlice<T> and &[T] have the same layout.
            Some(unsafe { &*(s as *const [T] as *const NonEmptySlice<T>) })
        }
    }

    /// Returns the first element.
    pub fn first(&self) -> &T {
        // SAFETY: guaranteed to have one element
        unsafe { self.0.get_unchecked(0) }
    }

    /// All but the first element. May be empty.
    pub fn tail(&self) -> &[T] {
        &self.0[1..]
    }

    /// Last element — guaranteed to exist.
    pub fn last(&self) -> &T {
        // SAFETY: invariant guarantees len >= 1
        unsafe { self.0.get_unchecked(self.0.len() - 1) }
    }

    /// Split into (first, tail) — a total function, no Option!
    pub fn split_first(&self) -> (&T, &[T]) {
        (self.first(), self.tail())
    }

    /// Always returns `false`, a `NonEmptySlice` is guaranteed
    /// non-empty.
    pub fn is_empty(&self) -> bool {
        false
    }

    /// Returns the number of elements (always greater than zero)
    pub fn len(&self) -> usize {
        self.0.len()
    }

    pub fn as_slice(&self) -> &[T] {
        &self.0
    }

    pub fn iter(&self) -> std::slice::Iter<'_, T> {
        self.0.iter()
    }
}

impl<T> std::ops::Deref for NonEmptySlice<T> {
    type Target = [T];
    fn deref(&self) -> &[T] {
        &self.0
    }
}

impl<T> AsRef<[T]> for NonEmptySlice<T> {
    fn as_ref(&self) -> &[T] {
        &self.0
    }
}

impl<T: std::fmt::Debug> std::fmt::Debug for NonEmptySlice<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(&self.0, f)
    }
}

impl<T, I: std::slice::SliceIndex<[T]>> std::ops::Index<I> for NonEmptySlice<T> {
    type Output = I::Output;
    fn index(&self, index: I) -> &Self::Output {
        &self.0[index]
    }
}

impl<'a, T> IntoIterator for &'a NonEmptySlice<T> {
    type Item = &'a T;
    type IntoIter = std::slice::Iter<'a, T>;
    fn into_iter(self) -> Self::IntoIter {
        self.0.iter()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn nes(s: &[u8]) -> &NonEmptySlice<u8> {
        NonEmptySlice::new(s).expect("test slice must be non-empty")
    }

    #[test]
    fn new_rejects_empty() {
        let empty: [u8; 0] = [];
        assert!(NonEmptySlice::new(&empty).is_none());
        assert!(NonEmptySlice::new(b"x").is_some());
    }

    #[test]
    fn len_and_first_and_last() {
        let s = nes(b"abc");
        assert_eq!(s.len(), 3);
        assert_eq!(s.first(), &b'a');
        assert_eq!(s.last(), &b'c');

        let single = nes(b"z");
        assert_eq!(single.len(), 1);
        assert_eq!(single.first(), &b'z');
        assert_eq!(single.last(), &b'z');
    }

    #[test]
    fn tail_and_split_first() {
        let s = nes(b"abc");
        assert_eq!(s.tail(), b"bc");

        let (first, tail) = s.split_first();
        assert_eq!(first, &b'a');
        assert_eq!(tail, b"bc");

        // Single-element slice: tail is empty but must not panic
        let single = nes(b"a");
        assert_eq!(single.tail(), b"");
        let (first, tail) = single.split_first();
        assert_eq!(first, &b'a');
        assert_eq!(tail, []);
    }

    #[test]
    fn is_empty_is_always_false() {
        assert!(!nes(b"a").is_empty());
        assert!(!nes(&[0u8; 64]).is_empty());
    }

    #[test]
    fn as_slice_and_deref() {
        let s = nes(b"foobar");
        assert_eq!(s.as_slice(), b"foobar");
        // Deref<Target = [T]>
        assert_eq!(&s[..3], b"foo");
        assert_eq!(s.iter().copied().collect::<Vec<_>>(), b"foobar".to_vec());
    }

    #[test]
    fn index() {
        let s = nes(b"abc");
        assert_eq!(s[0], b'a');
        assert_eq!(s[2], b'c');
        assert_eq!(&s[1..3], b"bc");
    }

    #[test]
    fn as_ref() {
        let s = nes(b"abc");
        let r: &[u8] = s.as_ref();
        assert_eq!(r, b"abc");
    }

    #[test]
    fn into_iterator() {
        let s = nes(b"abc");
        let collected: Vec<u8> = s.into_iter().copied().collect();
        assert_eq!(collected, b"abc".to_vec());
        // for-loop uses IntoIterator on &NonEmptySlice
        let mut sum = 0u8;
        for b in nes(&[1, 2, 3]) {
            sum += b;
        }
        assert_eq!(sum, 6);
    }

    #[test]
    fn debug_matches_slice() {
        assert_eq!(format!("{:?}", nes(b"abc")), format!("{:?}", b"abc"));
    }

    #[test]
    fn works_on_subslice() {
        // NonEmptySlice over a subslice of a larger buffer
        let data = [0u8, 1, 2, 3, 4];
        let sub = NonEmptySlice::new(&data[2..]).unwrap();
        assert_eq!(sub.first(), &2);
        assert_eq!(sub.last(), &4);
        assert_eq!(sub.len(), 3);
        // tail of a subslice from the middle
        assert_eq!(NonEmptySlice::new(&data[1..4]).unwrap().tail(), &[2, 3]);
    }

    #[test]
    fn generic_over_types() {
        let words = ["alpha", "beta"];
        let s = NonEmptySlice::new(&words).unwrap();
        assert_eq!(s.first(), &"alpha");
        assert_eq!(s.last(), &"beta");
        assert_eq!(s.len(), 2);
    }
}
