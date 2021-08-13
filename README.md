# lua-packer
Yet another binary data packing library for Lua 5.1/5.2. Includes two functions:

```lua
data              = packer.pack(fmt, ...)
nextstartpos, ... = packer.unpack(fmt, data, [startpos = 1])
```

## Format specifiers ##

### Control ###

Note: data endianness, if changed, is not remembered between function calls. Each `pack` or `unpack` will start off in native-endian.

    <

Switches to little-endian.

    >

Switches to big-endian.

    =

Switches to native byte order.

### Data ###

    c

A single char. This is for convenience, because `pack("B", string.byte(c))` is ugly.

    b/B

Signed/unsigned bytes.

    h/H

Signed/unsigned 2-byte shorts.

    i/I[N]

Signed/unsigned ints. N specifies the number of bytes (default: `N=4`). Cannot write ints larger than 8.

    l/L

Signed/unsigned 8-byte longs. Not entirely useful, since Lua can't represent full 64-bit integers, but included for completeness.

    f
    d

Single- and double-precision floats.

    z

A null-terminated string.

    s[N]

A fixed-length string, N bytes long (default: `N=2`). Too-short strings will be padded out to length with `'\0'` before packing.

If N is not given, this will read to the end of the string.

    p[N]

A variable-length string. Its length is prefixed as an unsigned `N`-byte int. `pack("p3", "Hello world")` is identical to `pack("I3 s", 11, "Hello world")`.

    x[N,V]

`N` bytes of padding (default: N=1). If `V` is given, this is the decimal value to use when writing (default: `V=0`).
