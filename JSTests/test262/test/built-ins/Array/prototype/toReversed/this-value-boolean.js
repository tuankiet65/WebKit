// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-array.prototype.toreversed
description: >
  Array.prototype.toReversed converts booleans to objects
info: |
  Array.prototype.toReversed ( )

  1. Let O be ? ToObject(this value).
  2. Let len be ? LengthOfArrayLike(O).
  ...
features: [change-array-by-copy]
includes: [compareArray.js]
---*/

assert.compareArray(Array.prototype.toReversed.call(true), []);
assert.compareArray(Array.prototype.toReversed.call(false), []);

/* Add length and indexed properties to `Boolean.prototype` */
Boolean.prototype.length = 3;
assert.compareArray(Array.prototype.toReversed.call(true), [undefined, undefined, undefined]);
assert.compareArray(Array.prototype.toReversed.call(false), [undefined, undefined, undefined]);
delete Boolean.prototype.length;
Boolean.prototype[0] = "monkeys";
Boolean.prototype[2] = "bogus";
assert.compareArray(Array.prototype.toReversed.call(true), []);
assert.compareArray(Array.prototype.toReversed.call(false), []);
