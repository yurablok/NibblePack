# NibblePack Wire Format Specification

## 1. Conventions

- The stream is a sequence of 4-bit units (**nibbles**). The first nibble of a
  byte is its bits 7..4 (MSB-first), the second is bits 3..0.
- **Containers** are `End`-terminated (no length prefix). Array elements and map
  key/value pairs are read until `End`.
- **Strings** are `NUL`-terminated and zero-copy.
- **Bytes** are length-prefixed and also zero-copy.
- **Integers** are delta-coded and lossless. **Floats** are delta-coded and
  lossy by default (`tolerance = 0.01 %`); `tolerance = 0` makes them lossless too.
- The int chain and the float chain are **separate** second-order predictors;
  each has a single global base for the whole top-level value.
- Multi-bit fields (varint payloads, trail bytes) are big-endian.

## 2. Grammar

```abnf
stream     := Tag                          ; one top-level value
element    := TagV                         ; inside containers
special    := TagV::Tag Tag                ; False .. ResetToIntZero
chain-special := TagV::Tag Tag::ResetTo TagR   ; three nibbles: E, D, TagR
container  := (TagV::Tag)? (Array | Map) element* TagV::End
```

- `Tag` alone means any member of the `Tag` enum; `Tag::X` means that
  specific nibble value. The same holds for `TagV` and `TagR`.

- The **top level** is a `Tag` nibble directly (usually `Tag::Array` or
  `Tag::Map`).
- Every **container element** (and every nested container header) is a `TagV`
  nibble; a nested array/map starts with `TagV::Tag` followed by a `Tag`
  nibble.
- After the closing `Tag::End` of the top-level container the writer pads the
  stream to a byte boundary with `End` nibbles (`1111`).

## 3. Tag (the type nibble)

Emitted at the top level, and after `TagV::Tag` for nested values.

```
0000  Null              the null value
0001  Array             array, elements follow, TagV::End-terminated
0010  Map               map, key/value pairs follow, TagV::End-terminated
0011  String            NUL-terminated, zero-copy (see §7)
0100  Bytes             length-prefixed (see §7)
0101  False             boolean false
0110  True              boolean true
0111  NaN               quiet NaN, the chains are not touched
1000  pInf              +infinity, the chains are not touched
1001  mInf              -infinity, the chains are not touched
1010  ResetToValue      the chain resets by the next TagV (see §5/§6)
1011  ResetToFloatZero  +0.0 decoded; pushed into the float chain (§6)
1100  ResetToIntZero    0 decoded; pushed into the int chain (§5)
1101  ResetTo           one TagR nibble follows (see §5)
1110  Reserved_0        must not appear on the wire
1111  End               end of container / end of stream
```

`TagR` (after `Tag::ResetTo`): `0000` Int32Min, `0001` Int32Max,
`0010` UInt32Max, `0011` Int64Min, `0100` Int64Max, `0101` UInt64Max.
The decoded `TagR` constant re-seeds both int chain values; the zero
constants (`ResetToIntZero` / `ResetToFloatZero`) instead shift the chain
like a delta with the value 0 (§5).

## 4. TagV (the value nibble: container elements)

```
0000  \
....   small int        int deltas -4..+3 (absolute after a reset)
0111  /
10sx  VarInt            int delta (absolute after a reset), s = sign, x = payload
1100  VarFloat0         float: value == prediction (XOR == 0)
1101  VarFloat1         float: XOR != 0, mantissa + trailing zeros (§6)
1110  Tag               a nested Tag nibble follows
1111  End               end of container
```

## 5. Int encoding

The chain state is a pair of second-order values `p1`, `p2` over **Int65** —
a 65-bit sign-magnitude integer (`[-UINT64_MAX, +UINT64_MAX]`, zero is
canonical `+0`), which covers both the int64 and uint64 ranges.

There are two independent chains following the same rules: the **value
chain** (int values and int map keys) and the **size chain** (the sizes of
bytes, §7). Sizes predict sizes, so a blob payload never disturbs the
prediction for the values, and vice versa; repeated chunk sizes ride as a
delta 0.

```
base  = 2*p1 - p2
delta = value - base
```

Any overflow of the Int65 arithmetic forces the absolute encoding. Also, the absolute encoding is applied when it is cheaper than the delta. The chain then shifts:
- after a **delta**: `p2 <- p1`, `p1 <- value` (second order);
- after an **absolute or reset**: `p1 = p2 = value` (the next delta
  degenerates to a first difference);
- the **zero constants** (`ResetToIntZero` / `ResetToFloatZero`) follow the
  delta rule with the value `0`: `p2 <- p1`, `p1 <- 0`, pushed into the
  respective chain (the size chain inside a bytes header). The second-order
  prediction continues through the zero, so on a linear crossing it hits the
  next sample exactly (the next delta is 0).

### Classes

| delta range                      | class      | pattern                          | nibbles | bytes |
|----------------------------------|------------|----------------------------------|---------|-------|
| -4 .. +3                         | int3       | `Int_p0`..`Int_m1` (tag^4)−4     | 1       | 0.5   |
| +4 .. +19 / -5 .. -20            | int5       | `10xx` `0xxx` (5-bit code)       | 2       | 1     |
| up to ±127                       | int7       | `10sx` `1xxx` `0xxx`             | 3       | 1.5   |
| up to ±2^10-1                    | int10      | `10sx` `1xxx` `1xxx` `0xxx`      | 4       | 2     |
| up to ±2^17-1                    | int17      | `10sx` `1xxx`*3 `0xxx` `xxxx`    | 6       | 3     |
| up to ±2^27-1                    | int27      | `10sx` `1xxx`*5 `0xxx` `xxxx`*2  | 9       | 4.5   |
| up to ±2^38-1                    | int38      | `10sx` `1xxx`*6 `0xxx` `xxxx`*4  | 12      | 6     |
| up to ±2^49-1                    | int49      | `10sx` `1xxx`*7 `0xxx` `xxxx`*6  | 15      | 7.5   |
| up to ±2^64-1                    | int64      | `10sx` `1xxx`*4 `0xxx` `xxxx`*12 | 18      | 9     |

- For `int7`..`int64`: `s` is the sign (`0` = +, `1` = -), `x` is the top bit
  of the magnitude, the rest travels in the 3-bit groups and 4-bit nibbles
  (§7). The magnitude is sent without an implicit 1.

### Constant tokens

Fired only when **cheaper** than the chosen delta/absolute encoding:

- value `0` => `TagV::Tag` + `Tag::ResetToIntZero` — 2 nibbles;
- one of the six extremes => `TagV::Tag` + `Tag::ResetTo` + TagR — 3 nibbles.

### Absolute

`TagV::Tag` + `Tag::ResetToValue` + the varint of the value itself.  
Used when the delta is impossible or more expensive than the absolute (`nibblesCount + 2` nibbles).

## 6. Float encoding

The float chain keeps `p1`, `p2` (doubles) and the previous trailing-zeros
count. The prediction and the delta:

```
P = 2*p1 - p2
x = bits(q) XOR bits(P)        ; raw IEEE bits of the quantized value
```

- `x == 0` => **VarFloat0** (`1100`): the value equals the prediction; the
  chain pushes `P`. No field follows.
- `q == 0` => `TagV::Tag` + `Tag::ResetToFloatZero`: decodes +0.0 and pushes
  zero into the chain (`p2 <- p1`, `p1 <- 0`).
- otherwise => **VarFloat1** (`1101`):
  - `lead = countl_zero(x)`, `trail = countr_zero(x)`,
    `mlen = 64 - lead - trail` (the meaningful length of the XOR delta);
  - the payload is `v = x >> trail` (the bits above the trailing zeros,
    leading 1 included) in the int field format (§7), **zero-padded** at the
    top to the class size;
  - then the **trailing-zeros nibble(s)**: `d = trail - prevTrail`; if
    `-7 <= d <= 7` a single nibble `d + 7`; otherwise the escape `1111` plus
    an **absolute 8-bit trail** (`hi`, `lo` nibbles);
  - the chain pushes `q` and stores `trail` as the new reference.

### Classes

| mlen range | class    | pattern                                       | nibbles | bytes |
|------------|----------|-----------------------------------------------|---------|-------|
| up to 7    | float7   | `1101` `0xxx` `xxxx` + trail                  | 4       | 2     |
| up to 10   | float10  | `1101` `1xxx` `0xxx` `xxxx` + trail           | 5       | 2.5   |
| up to 17   | float17  | `1101` `1xxx`*2 `0xxx` `xxxx`*2 + trail       | 7       | 3.5   |
| up to 20   | float20  | `1101` `1xxx`*3 `0xxx` `xxxx`*2 + trail       | 8       | 4     |
| up to 27   | float27  | `1101` `1xxx`*4 `0xxx` `xxxx`*3 + trail       | 10      | 5     |
| up to 34   | float34  | `1101` `1xxx`*5 `0xxx` `xxxx`*4 + trail       | 12      | 6     |
| up to 41   | float41  | `1101` `1xxx`*6 `0xxx` `xxxx`*5 + trail       | 14      | 7     |
| up to 52   | float52  | `1101` `1xxx`*7 `0xxx` `xxxx`*7 + trail       | 17      | 8.5   |
| up to 63   | float63  | `1101` `1xxx`*8 `0xxx` `xxxx`*9 + trail       | 20      | 10    |
| up to 64   | float66  | `1101` `1xxx`*9 `0xxx` `xxxx`*9 + trail       | 21      | 10.5  |

trail (the count of trailing zeros of the XOR delta):
- `1111` `xxxx` `xxxx` => the escape + the absolute 8-bit trail (hi, lo)
- `xxxx` => `d = trail - prevTrail` in -7..+7: the nibble is `d + 7`

### Tolerance => k (the quantization grid)

`tolerance` is a fraction of the value (`0` = lossless; the default is
`0.0001 | 0.01 %`). The writer quantizes to the nearest point of the
k-significant-bit grid with
```c
k = ceil(-log2(tolerance)), capped to [1, 53]
```
computed straight from the IEEE exponent field (`1023 - exp`, subnormals and
out-of-range tolerances clamp to the caps: `tolerance <= 0` or NaN => lossless,
`+inf` => 1 = the coarsest grid, `-inf` => 53 = lossless).
The quantization rounds the raw bits half-up to multiples of `2^(53-k)`:
```c
q = ((bits(number) + (1 << (53-k-1))) >> (53-k)) << (53-k)
```
which preserves the sign and guarantees `|q - y| <= tolerance * |y|` per
sample (the drift budget).

## 7. String and bytes layout

```
String: TagV::Tag, Tag::String, align, raw bytes, NUL
Bytes:  TagV::Tag, Tag::Bytes, int value: the size, align, raw bytes
```

`align` pads with a **zero** nibble when the stream is mid-byte, so the raw
data always starts byte-aligned; the reader rejects a non-zero pad nibble.
An empty string is just the NUL. The `size` of bytes is a regular int
value over its own **size chain** (§5): a delta against the prediction,
or an absolute after a reset — the value chain is not touched.

## 8. Examples

### `[0, 1, -1, 0]` => `10 15 3F`

```cpp
0001  Tag::Array
0000  TagV::Int_p0   delta = 0 (cold chain: base = 0)
0001  TagV::Int_p1   delta = 1 - 0 = +1
0101  TagV::Int_m3   delta = -1 - 2 = -3                    (base was 2*1-0)
0011  TagV::Int_p3   delta = 0 - (-3) = +3                  (base was 2*(-1)-1)
1111  Tag::End
```

### `[1.0, -1.0, 0.0]` => `1D F7 FF 34 D8 97 FF 7E BF`

```cpp
0001  Tag::Array
1101  VarFloat1, x = bits(1.0) XOR bits(0) = 0x3FF0.., mlen = 10 => class (2,1)
1111 0111 1111  v = 0x3FF (bits above trail=52), zero-padded to 10 bits
1111 0011 0100  trail = 52: |d| > 7 => escape 1111 + absolute 0x34 = 52
1101  VarFloat1, x = bits(-1.0) XOR bits(2.0) = 0xFFF0.., mlen = 12 => (3,2)
1000 1001 0111 1111 1111  v = 0xFFF
0111  trail: d = 52 - 52 = 0 => nibble 0 + 7
1110  TagV::Tag
1011  Tag::ResetToFloatZero  (q = 0.0; pushes 0 into the chain)
1111  Tag::End
```

### `[INT32_MIN, 0]` => `1E D0 EC FF`

```cpp
0001  Tag::Array
1110  TagV::Tag
1101  Tag::ResetTo
0000  TagR::ResetToInt32Min   (4 nibbles beat the 20-nibble absolute)
1110  TagV::Tag
1100  Tag::ResetToIntZero     (delta would cost 14 nibbles, the token is 2)
1111  Tag::End
```

### `["hi", <12 34 56>]` => `1E 30 68 69 00 E4 30 12 34 56 FF`

```cpp
0001         Tag::Array
1110 0011    TagV::Tag + Tag::String
0000         align nibble
0110 1000    'h'
0110 1001    'i'
0000 0000    '\0'
1110 0100    TagV::Tag + Tag::Bytes
0011         size = 3 (cold-chain delta, Int_p3)
0000         align nibble
0001 0010    0x12
0011 0100    0x34
0101 0110    0x56
1111         TagV::End
1111         align nibble
```

### `{"a": 1, 2: "b"}` => `2E 30 61 00 10 E3 62 00 FF`

```cpp
0010         Tag::Map
1110 0011    TagV::Tag + Tag::String      key "a"
0000         align nibble
0110 0001    'a'
0000 0000    '\0'
0001         TagV::Int_p1                 value 1 (cold-chain delta +1)
0000         TagV::Int_p0                 key 2 (base = 2*1 - 0 = 2, delta = 0)
1110 0011    TagV::Tag + Tag::String      value "b"
0110 0010    'b'
0000 0000    '\0'
1111         TagV::End
1111         align nibble
```

## 9. Robustness contract

- The reader never reads past the buffer: every nibble read is bounds-checked
  (container ends, varint fields, trail bytes); a truncated stream simply
  ends the parse.
- The alignment nibble must be `0`; otherwise the parse stops.
- Unknown nibble values stop the parse (an assert fires in debug builds).
  TODO: Error handling.
- Int deltas are exact; float drift is bounded by `tolerance * |value|` per
  sample.
