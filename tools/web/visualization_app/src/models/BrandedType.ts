// Copyright (c) Meta Platforms, Inc. and affiliates.

declare const __brand: unique symbol;
declare const __child_brand: unique symbol;

/**
 * A "branded" type is an extension of a base type that is not inter-convertible to the base without using `as` or to another brand.
 * https://www.learningtypescript.com/articles/branded-types#ts-brand
 */
export type BrandedType<T, B> = T & {[__brand]: B};
export type BrandedChildType<T, C> = T & {[__child_brand]: C}; // when you want to declare a branded version of a branded type
