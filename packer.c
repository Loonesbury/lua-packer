/*
	yet another string-packing lib for Lua 5.1 & 5.2
	compiles with -std=c89 -Wall on GCC 6.3.0, somehow.

	funcs:
		binstr       = packer.pack(fmt, ...)
		nextpos, ... = packer.unpack(fmt, binstr, [startpos])

	format:
		<		use little endian
		>		use big endian
		=		use native endian

		c		char (accepts and returns a 1-char string)
		b/B		signed/unsigned byte
		h/H		signed/unsigned short
		l/L		signed/unsigned long
		i/I[N]	signed/unsigned int (default: N=4)
		f		float
		d		double

		z		zero-terminated string
		s[N]	fixed-length string; if N isn't given, reads until EOF
		p[N]	string prefixed with N-byte length (default: N=2)

		x[N,V]	N bytes of 'V' (default: N=1, V=0)
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

typedef union value {
	int64_t  i64;
	int32_t  i32;
	int16_t  i16;
	int8_t   i8;

	uint64_t u64;
	uint32_t u32;
	uint16_t u16;
	uint8_t  u8;

	size_t   sz;

	float    f;
	double   d;

	char*    s;
	char     tab[8];
} value;

/* host byte order */
union {
	uint32_t blank;
	uint8_t little;
} hostendian = {1};

/* read number from `str`, increment pointer */
/* if no number present, returns `defaultsize' */
static size_t read_number(const char **str, size_t defaultsize)
{
	const char *s = *str;

	if (isdigit(*s)) {
		const char *ss = s;
		size_t size = 0;
		int mul = 1;

		while (isdigit(*s))
			s++;
		*str = s--;

		mul = 1;
		while (s >= ss) {
			size += (*s - '0')*mul;
			mul *= 10;
			s--;
		}

		return size;
	} else {
		return defaultsize;
	}
}

/* swaps `p's byte order */
static void swap_endian(void *p, size_t sz)
{
	unsigned char *bytes, i, j, swp;
	for (bytes = p, i = 0, j = sz - 1; i < sz/2; i++, j--) {
		swp = bytes[i];
		bytes[i] = bytes[j];
		bytes[j] = swp;
	}
}

/*
#define PACK_REALIGN()\
{\
	if (pos % alignment) {\
		int added = alignment - pos % alignment;\
		luaL_addlstring(buf, alignbuf, added);\
		pos += added;\
	}\
}
*/

/*
* suppose we pack an int32 as an int24:
* LE: (EF BE AD)DE 00 00 00 00
* BE:  00 00 00 00 DE(AD BE EF)
*
*/
#define PACK_NUMBER(n, var, size)\
{\
	var = n;\
\
	if (hostendian.little != little)\
		swap_endian(&var, sizeof(var));\
	if (!little)\
		luaL_addlstring(buf, (char*)&var + (sizeof(var) - size), size);\
	else\
		luaL_addlstring(buf, (char*)&var, size);\
\
	pos += size;\
}

#define PACK_STRING(str, len)\
{\
	luaL_addlstring(buf, str, len);\
	pos += len;\
}

#define CASE_PACKNUMBER(c, var, size)\
	case c: {\
		PACK_NUMBER(luaL_checknumber(L, argi++), var, size);\
		break;\
	}

/*
* binstr = packer.pack(fmt, ...)
*/
static int l_pack(lua_State *L)
{
	const char *f, *fend;
	unsigned int argi = 2, pos = 0 /*, alignment = 1*/;
	size_t size, fmtlen;
	char c, little = hostendian.little;
	luaL_Buffer buf_, *buf;
	value v;

	luaL_checktype(L, 1, LUA_TSTRING);
	f = lua_tolstring(L, 1, &fmtlen);
	fend = f + fmtlen;

	buf = &buf_;
	luaL_buffinit(L, buf);

	while (f < fend) {
		c = *f++;
		switch(c) {
			/* control characters */
			case '<': little = 1; break;
			case '>': little = 0; break;
			case '=': little = hostendian.little; break;
			/* case '!': {
				alignment = read_number(&f, 1);
				if (alignment == 0)
					alignment = 1;
				if (alignment > sizeof(alignbuf))
					luaL_error(L, "'%c%d': alignment too large", c, alignment);
				break;
			} */

			/* char (for convenience) */
			case 'c':
				/* do not convert a number into a string */
				luaL_checktype(L, argi, LUA_TSTRING);
				luaL_addchar(buf, *lua_tostring(L, argi++));
				pos++;
				break;

			/* byte */
			CASE_PACKNUMBER('b', v.i8, 1)
			CASE_PACKNUMBER('B', v.u8, 1)

			/* short */
			CASE_PACKNUMBER('h', v.i16, 2)
			CASE_PACKNUMBER('H', v.u16, 2)

			/* long */
			CASE_PACKNUMBER('l', v.i32, 2)
			CASE_PACKNUMBER('L', v.u32, 2)

			/* floats */
			CASE_PACKNUMBER('f', v.f, 4);
			CASE_PACKNUMBER('d', v.d, 8);

			/* ints gots to be DIFFERENT */
			case 'i':
			case 'I':
				size = read_number(&f, 4);
				if (size <= 0) {
					/* 0-byte int? ignore. */
					argi++;
					break;
				}
				if (size > 8)
					luaL_error(L, "'%c%d': size is too wide", c, size);

				if (c == 'I')
					PACK_NUMBER(luaL_checknumber(L, argi++), v.i64, size)
				else
					PACK_NUMBER(luaL_checknumber(L, argi++), v.u64, size)
				break;

			/* NULL-terminated string */
			case 'z': {
				const char *str;
				size_t len;

				luaL_checktype(L, argi, LUA_TSTRING);
				str = lua_tolstring(L, argi++, &len);

				luaL_addlstring(buf, str, len + 1);
				size += len + 1;
				break;
			}
			/* length-prefixed string */
			case 'p': {
				const char *str;
				size_t len;

				/* size of length prefix */
				size = read_number(&f, 2);
				if (size > 4)
					luaL_error(L, "'%c%d': int is too wide", c, size);

				luaL_checktype(L, argi, LUA_TSTRING);
				str = lua_tolstring(L, argi++, &len);

				/*
				* chop the length down to something that will fit in the
				* specified integer size
				*/
				len = len & (~(uint32_t)0 >> (4 - size*8));

				PACK_NUMBER(len, v.u32, size);
				PACK_STRING(str, len);
				break;
			}
			/* fixed-length string */
			case 's': {
				const char *str;
				char *bufstr;
				size_t len, inputlen;

				luaL_checktype(L, argi, LUA_TSTRING);
				str = lua_tolstring(L, argi++, &inputlen);

				/* if no length was given, we just pack the whole thing */
				/* size_t is unsigned, or else we could just use default=-1... */
				if (isdigit(*f))
					len = read_number(&f, 0);
				else
					len = inputlen;

				if (len == 0) {
					argi++;
					break;
				}

				/* if the arg is too long, it'll be clipped */
				/* if the arg is too short, it'll be padded with NULLs */
				bufstr = (char*)malloc(len);
				memset(bufstr, 0, len);
				memcpy(bufstr, str, min(len, inputlen));
				luaL_addlstring(buf, bufstr, len);
				free(bufstr);

				pos += len;
				break;
			}

			/* padding */
			case 'x': {
				char val = '\0';
				char *bufstr;

				size = read_number(&f, 1);
				if (size > LUAL_BUFFERSIZE)
					luaL_error(L, "'%c%d': padding is too long", c, size);

				if (*f == ',') {
					f++;
					val = read_number(&f, 0);
				}

				bufstr = luaL_prepbuffer(buf);
				memset(bufstr, val, size);
				luaL_addsize(buf, size);

				pos += size;
				break;
			}
			/* alignment snap */
			/* case 'X':
				size = read_number(&f, 0);
				if (size > 1)
					PACK_REALIGN(size);
				break; */
			case ' ':
				break;
			default:
				luaL_error(L, "invalid format specifier '%c'", c);
		}
	}

	luaL_pushresult(buf);
	return 1;
}

#define UNPACK_REALIGN() {\
	int misaligned = pos % alignment;\
	if (misaligned) {\
		d += alignment - misaligned;\
	}\
}

#define CHECK_DATA(size) {\
	if (d + (size) > dend)\
		luaL_error(L, "hit end of data while reading '%c' (%d)", c, lua_gettop(L) - top);\
}

#define UNPACK_NUMBER(var, size, push)\
{\
	CHECK_DATA(size);\
	v.u64 = 0;\
	if (!little)\
		memcpy(v.tab + (8 - size), d, size);\
	else\
		memcpy(v.tab, d, size);\
\
	if (little != hostendian.little)\
		swap_endian(v.tab, 8);\
\
	if (push)\
		lua_pushnumber(L, var);\
	d += size;\
}

#define CASE_UNPACKNUMBER(c, var, size)\
	case c: {\
		UNPACK_NUMBER(var, size, 1)\
		break;\
	}

/*
* nextpos, ... = packer.unpack(fmt, binstr)
*/
static int l_unpack(lua_State *L)
{
	const char *fmt = luaL_checkstring(L, 1);
	const char *f = fmt;
	const char *data, *d, *dend;
	size_t datalen;

	unsigned int nret, top = lua_gettop(L);
	size_t size;
	char c, little = hostendian.little;
	value v;

	luaL_checktype(L, 2, LUA_TSTRING);
	data = d = lua_tolstring(L, 2, &datalen);
	dend = d + datalen;

	if (lua_gettop(L) > 2)
		d += luaL_checkinteger(L, 3) - 1;

	top = lua_gettop(L);

	/* reserve space for `nextpos' */
	lua_pushnil(L);

	while (d < dend && (c = *f++)) {
		switch(c) {
			/* control characters */
			case '<': little = 1; break;
			case '>': little = 0; break;
			case '=': little = hostendian.little; break;
			/* case '!': {
				alignment = read_number(&f, 1);
				if (alignment == 0)
					alignment = 1;
				if (alignment > sizeof(alignbuf))
					luaL_error(L, "'%c%d': alignment too large", c, alignment);
				break;
			} */

			/* char (for convenience) */
			case 'c':
				CHECK_DATA(1);
				lua_pushlstring(L, d++, 1);
				break;

			/* byte */
			CASE_UNPACKNUMBER('b', v.i8, 1)
			CASE_UNPACKNUMBER('B', v.u8, 1)
			/* short */
			CASE_UNPACKNUMBER('h', v.i16, 2)
			CASE_UNPACKNUMBER('H', v.u16, 2)
			/* long */
			CASE_UNPACKNUMBER('l', v.i64, 8)
			CASE_UNPACKNUMBER('L', v.u64, 8)
			/* floats */
			CASE_UNPACKNUMBER('f', v.f, 4)
			CASE_UNPACKNUMBER('d', v.d, 8)

			case 'i':
			case 'I':
				size = read_number(&f, 4);
				if (size <= 0) {
					/* 0-byte int? ignore. */
					lua_pushinteger(L, 0);
					break;
				}
				if (size > 8)
					luaL_error(L, "'%c%d': size is too wide", c, size);

				if (c == 'I')
					UNPACK_NUMBER(v.u32, size, 1)
				else
					UNPACK_NUMBER(v.i32, size, 1)
				break;

			/* NULL-terminated string */
			case 'z':
				size = strlen(d);
				/*
				* hurk. the lua string has a NULL at the end, so strlen(d)
				* is safe, but we have to act like it isn't there.
				*/
				CHECK_DATA(size + 1);
				lua_pushlstring(L, d, size);
				d += size + 1;
				break;
			/* length-prefixed string */
			case 'p': {
				/* size of length prefix */
				size = read_number(&f, 2);
				if (size > 4)
					luaL_error(L, "'%c%d': int is too wide", c, size);

				CHECK_DATA(size);
				UNPACK_NUMBER(v.u32, size, 0)

				CHECK_DATA(v.u32);
				lua_pushlstring(L, d, v.u32);
				d += v.u32;
				break;
			}
			/* fixed-length string */
			case 's':
				if (isdigit(*f))
					size = read_number(&f, 0);
				else
					/* no size given, read until EOF */
					size = max(0, dend - d);

				CHECK_DATA(size);
				lua_pushlstring(L, d, size);
				d += size;
				break;

			/* padding */
			case 'x': {
				size = read_number(&f, 1);

				CHECK_DATA(size);
				d += size;
				break;
			}
			/* alignment snap */
			/* case 'X':
				break; */
			case ' ':
				break;
			default:
				luaL_error(L, "invalid format specifier '%c'", c);
		}
	}

	nret = lua_gettop(L) - top;
	lua_pushinteger(L, d - data + 1);
	lua_replace(L, -nret - 1);

	return lua_gettop(L) - top;
}

static const luaL_Reg packlib[] = {
	{"pack", l_pack},
	{"unpack", l_unpack},
	{NULL, NULL},
};

int luaopen_packer(lua_State *L)
{
#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, packlib);
#else
	lua_createtable(L, 0, 3);
	luaL_register(L, NULL, packlib);
#endif
	return 1;
}
