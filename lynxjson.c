#include "lynxjson.h"
#include <assert.h>
#include <stdlib.h> //NULL, strtod(), malloc(), realloc(), free()
#include <math.h>	//HUGE_VAL
#include <errno.h>	//errno, ERANGE
#include <string.h>	//memcpy()
#include <stdio.h>	//sprintf()

//为了减少解析解析函数之间传递的参数个数，把这些参数都放进一个结构体中
typedef struct {
	const char* json;	//指向当前处理的位置
	char* stack;	//在解析字符串、数组、对象等未知大小的元素时使用
	size_t size, top;//栈的容量及栈顶
} lynx_context;

//可以在编译选项中自行设置宏，没有设置的话就使用缺省值
#ifndef LYNX_PARSE_STACK_INIT_SIZE
#define LYNX_PARSE_STACK_INIT_SIZE (1 << 8) //栈的初始容量（字节）
#endif

//进栈指定的字节数，返回指向栈顶内存的指针（以方便赋值操作）
//注意：不要保存此函数的返回值！
//当栈扩容后，用户之前保存的指向栈中元素的指针会失效！
static void* lynx_context_push(lynx_context* c, size_t size)
{
	void* ret;
	assert(size > 0);
	if (c->top + size >= c->size) {
		if (c->size == 0) {
			c->size = LYNX_PARSE_STACK_INIT_SIZE;
		}
		while (c->top + size >= c->size) {
			c->size += c->size >> 1;
		}
		c->stack = (char*)realloc(c->stack, c->size);
	}
	ret = c->stack + c->top;
	c->top += size;
	return ret;
}

//出栈指定的字节数，返回出栈之后的栈顶指针
static void* lynx_context_pop(lynx_context* c, size_t size)
{
	assert(c->top >= size);
	return c->stack + (c->top -= size);
}


#define EXPECT(c, ch) do {\
	assert(*((c)->json) == (ch));\
	(c)->json++;\
} while(0)

//ws = *(%x20 / %x09 / %x0A / %x0D)
//跳过连续的空白字符, 此函数不会出错
static void lynx_parse_whitespace(lynx_context* c)
{
	const char *p = c->json;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		++p;
	c->json = p;
}

//解析字面量，如false，true，null
static int lynx_parse_literal(lynx_context* c, lynx_value* v, const char* s, lynx_type t)
{
	EXPECT(c, *s);
	const char *p = s + 1, *q = c->json;
	size_t len = 0;
	while (*p) {
		if (*p++ != *q++) {
			return LYNX_PARSE_INVALID_VALUE;
		}
		++len;
	}
	c->json += len;
	v->type = t;
	return LYNX_PARSE_OK;
}

#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')	//宏函数中的参数不要带++，--
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')
#define ISHEXATOF(ch)		((ch) >= 'A' && (ch) <= 'F')
#define ISHEX(ch)		(ISDIGIT(ch) || ((ch) >= 'a' && (ch) <= 'f') || ISHEXATOF(ch))

static unsigned hex_to_dec(char ch)
{
	if (ISDIGIT(ch)) return ch - '0';
	if (ISHEXATOF(ch)) return ch - 'A' + 10;
	return ch - 'a' + 10;
}

#define PUTC(c, ch) do { *(char*)lynx_context_push((c), sizeof(char)) = (ch); } while (0)

//将码点翻译为对应的UTF8编码进栈
static void lynx_encode_utf8(lynx_context* c, unsigned u)
{
	assert(0x0000 <= u && u <= 0x10FFFF);
	/*
	UTF-8为变长度编码，不同的码点有不同的编码长度和规则：
		U+0000 - U+007F 为ASCII编码区，一字节编码，码点与编码一致，如U+0041 -> 01000001，编码为0xxxxxxx
		U+0080 - U+07FF 为带有变音符号的拉丁文、希腊文、西里尔字母、亚美尼亚语、希伯来文、阿拉伯文、叙利亚文
	等字母需要2字节编码。110xxxxx 10xxxxxx
		U+0800 - U+FFFF 如中日韩方块文、东南亚文字、中东文字等，使用3字节编码。1110xxxx 10xxxxxx 10xxxxxx
		U+10000 - U+10FFFF 极少使用的语言字符，使用4字节编码。11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
	*/

	//x & 0xFF这种操作是为了避免编译器的警告
	if (u <= 0x007F) {
		PUTC(c, u & 0xFF);	//感谢gdb
	} else											
	if (u <= 0x07FF) {
		PUTC(c, 0xC0 | ((u >> 6) & 0xFF));		//0xC0 = 11000000	
		PUTC(c, 0x80 | (u & 0x3F));				//0x3F = 00111111
	} else										//0x80 = 10000000
	if (u <= 0xFFFF) {
		PUTC(c, 0xE0 | ((u >> 12) & 0x0F));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	} else
	if (u <= 0x10FFFF) {
		PUTC(c, 0xF0 | ((u >> 18) & 0xF7));
		PUTC(c, 0x80 | ((u >> 12) & 0x3F));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	}
}

//解析字符串中\u后面的四位十六进制数
//成功返回p的位置，失败返回NULL
static const char* lynx_parse_hex4(const char* p, unsigned *u)
{
	*u = 0;
	for (int i = 0; i < 4; ++i) {
		*u <<= 4;
		if (ISHEX(p[i])) *u |= hex_to_dec(p[i]);
		else return NULL;
	}
	return p + 4;
}	

//为了简单起见，使用标准库的strtod()将读取的字符串数字转化为浮点数
static int lynx_parse_number(lynx_context* c, lynx_value* v)
{
	//检验数字是否符合JSON规范
	//number = [ "-" ] int [ frac ] [ exp ]
	//int = "0" / digit1-9 *digit
	//frac = "." 1*digit
	//exp = ("e" / "E") ["-" / "+"] 1*digit
	const char *p = c->json;
	if (*p == '-') ++p;
	if (ISDIGIT(*p)) {
		if (ISDIGIT1TO9(*p)) for (++p; ISDIGIT(*p); ++p);
		else ++p;
	} else {
		return LYNX_PARSE_INVALID_VALUE;
	}
	if (*p == '.') {
		++p;
		if (ISDIGIT(*p)) {
			for (++p; ISDIGIT(*p); ++p);
		} else {
			return LYNX_PARSE_INVALID_VALUE;
		}
	}
	if (*p == 'e' || *p == 'E') {
		++p;
		if (*p == '+' || *p == '-') ++p;
		if (ISDIGIT(*p)) for (++p; ISDIGIT(*p); ++p);
		else return LYNX_PARSE_INVALID_VALUE;
	}
	errno = 0;
	v->u.n = strtod(c->json, NULL);
	if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
		return LYNX_PARSE_NUMBER_TOO_BIG;
	c->json = p;
	v->type = LYNX_NUMBER;
	return LYNX_PARSE_OK;
}

//应保证运行函数之前和之后栈的状态不变（top）
#define STRING_ERROR(ret) do { c->top = head; return ret; } while (0)

static int lynx_parse_string_raw(lynx_context* c, char** str, size_t* len)

{
	size_t head = c->top;
	const char* p;
	unsigned u, ul;
	EXPECT(c, '\"');
	p = c->json;
	while (1) {
		char ch = *p++;
		switch (ch) {
			case '\\': {
				switch(*p++) {
					case '\"': 	PUTC(c, '\"'); break;
					case '\\': 	PUTC(c, '\\'); break;
					case '/': 	PUTC(c, '/');  break;
					case 'b': 	PUTC(c, '\b'); break;
					case 'f': 	PUTC(c, '\f'); break;
					case 'n': 	PUTC(c, '\n'); break;
					case 'r': 	PUTC(c, '\r'); break;
					case 't': 	PUTC(c, '\t'); break;
					case 'u':
						if (!(p = lynx_parse_hex4(p, &u)))
							return LYNX_PARSE_INVALID_UNICODE_HEX;
						//处理代理对
						if (0xD800 <= u && u <= 0xDBFF) {
							//如果是高代理项，则下一个字符应是低代理项，才能得到正确的码点
							if (*p++ != '\\') STRING_ERROR(LYNX_PARSE_INVALID_UNICODE_SURROGATE);
							if (*p++ != 'u') STRING_ERROR(LYNX_PARSE_INVALID_UNICODE_SURROGATE);
							if (!(p = lynx_parse_hex4(p, &ul)))
								return LYNX_PARSE_INVALID_UNICODE_HEX;
							if (0xDC00 <= ul && ul <= 0xDFFF) {
								u = 0x10000 + (u - 0xD800) * 0x400 + (ul - 0xDC00);
							} else {
								return LYNX_PARSE_INVALID_UNICODE_SURROGATE;
							}
						}
						lynx_encode_utf8(c, u);
						break;
					default:
						STRING_ERROR(LYNX_PARSE_INVALID_STRING_ESCAPE);
				}
				break;
			}
			case '\"': {
				*len = c->top - head;
				*str = lynx_context_pop(c, *len);
				c->json = p;
				return LYNX_PARSE_OK;
			}
			case '\0': {
				STRING_ERROR(LYNX_PARSE_MISS_QUOTATION_MARK);
			}
			default: {
				//不能出现控制字符如'\b'
				if ((unsigned char)ch < 0x20) {
					STRING_ERROR(LYNX_PARSE_INVALID_STRING_CHAR);
				}
				PUTC(c, ch);
			}
		}
	}
}

static int lynx_parse_string(lynx_context* c, lynx_value* v)
{
	char* s;
	size_t len;
	int ret = lynx_parse_string_raw(c, &s, &len);
	if (ret == LYNX_PARSE_OK) {
		lynx_set_string(v, s, len);
	}
	return ret;
}

//lynx_parse_array()与lynx_parse_value()相互调用，故加入前向声明
static int lynx_parse_value(lynx_context* c, lynx_value* v);

static int lynx_parse_array(lynx_context* c, lynx_value* v)
{
	size_t size = 0;
	int ret;
	EXPECT(c, '[');
	lynx_parse_whitespace(c);
	if (*c->json == ']') {
		++c->json;
		lynx_set_array(v, 0);
		return LYNX_PARSE_OK;
	}
	while (1) {
		lynx_value e;	//临时存放解析出的数组元素
		lynx_init(&e);
		ret = lynx_parse_value(c, &e);
		if (ret != LYNX_PARSE_OK) {
			break;
		}
		//把临时元素压栈
		memcpy(lynx_context_push(c, sizeof(lynx_value)), &e, sizeof(lynx_value));
		++size;
		lynx_parse_whitespace(c);
		if (*c->json == ']') {
			++c->json;
			lynx_set_array(v, size);
			v->u.a.size = size;
			size *= sizeof(lynx_value);
			memcpy(v->u.a.e, lynx_context_pop(c, size), size);
			return LYNX_PARSE_OK;
		} else
		if (*c->json == ',') {
			++c->json;
		} else {
			ret = LYNX_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
			break;
		}
		lynx_parse_whitespace(c);
	}
	//只有出错时才会运行下面的代码

	//释放栈中已解析的元素(特别是字符串、数组和对象等管理资源的JSON值)
	for (size_t i = 0; i < size; ++i) {
		lynx_free((lynx_value*)lynx_context_pop(c, sizeof(lynx_value)));
	}
	return ret;
}

static void lynx_set_string_raw(char** rs, size_t* rlen, const char* s, size_t len);
static int lynx_parse_string_raw(lynx_context* c, char** str, size_t* len);
static int lynx_parse_object(lynx_context* c, lynx_value* v)
{
	size_t size = 0;
	int ret;
	EXPECT(c, '{');
	lynx_parse_whitespace(c);
	if (*c->json == '}') {
		++c->json;
		lynx_set_object(v, 0);
		return LYNX_PARSE_OK;
	}

	while (1) {
		lynx_member m;	//临时存放解析出的成员
		m.k = NULL; m.klen = 0;
		lynx_init(&m.v);
		char* s; size_t len;
		if (*c->json != '\"') {
			ret = LYNX_PARSE_MISS_KEY;
			break;
		}
		ret = lynx_parse_string_raw(c, &s, &len);
		//这里的s指向栈中的字符串
		if (ret != LYNX_PARSE_OK) break;
		lynx_set_string_raw(&(m.k), &(m.klen), s, len);

		lynx_parse_whitespace(c);
		if (*c->json == ':') ++c->json;
		else {
			ret = LYNX_PARSE_MISS_COLON;
			free(m.k);
			break;
		}
		lynx_parse_whitespace(c);

		ret = lynx_parse_value(c, &m.v);
		if (ret != LYNX_PARSE_OK) {
			free(m.k);
			break;
		}

		//成功读取一个成员，压栈
		memcpy(lynx_context_push(c, sizeof(lynx_member)), &m, sizeof(lynx_member));
		++size;
		lynx_parse_whitespace(c);
		if (*c->json == '}') {
			++c->json;
			lynx_set_object(v, size);
			v->u.o.size = size;
			size *= sizeof(lynx_member);
			memcpy(v->u.o.m, lynx_context_pop(c, size), size);
			return LYNX_PARSE_OK;
		} else
		if (*c->json == ',') {
			++c->json;
		} else {
			ret = LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
			break;
		}
		lynx_parse_whitespace(c);
	}

	//出错后善后处理,销毁之前存在栈中的读取的成员
	for (size_t i = 0; i < size; ++i) {
		lynx_member* m = lynx_context_pop(c, sizeof(lynx_member));
		free(m->k); lynx_free(&m->v);
	}
	return ret;
}

//value = null / false / true / number /string /array /object
static int lynx_parse_value(lynx_context* c, lynx_value* v)
{
	switch (*c->json) {
		case '{':	return lynx_parse_object(c, v);
		case '[':	return lynx_parse_array(c, v);
		case 'n':   return lynx_parse_literal(c, v, "null", LYNX_NULL);
		case 't':   return lynx_parse_literal(c, v, "true", LYNX_TRUE);
		case 'f':   return lynx_parse_literal(c, v, "false", LYNX_FALSE);
		case '\"':	return lynx_parse_string(c, v);
		default:    return lynx_parse_number(c, v);
		case '\0':  return LYNX_PARSE_EXPECT_VALUE;
	}
}

int lynx_parse(lynx_value* v, const char* json)
{
	lynx_context c;
	int ret;
	assert(v != NULL);
	c.json = json;
	c.stack = NULL;
	c.size = c.top = 0;
	lynx_init(v);
	lynx_parse_whitespace(&c);
	ret = lynx_parse_value(&c, v);
	if (ret == LYNX_PARSE_OK) {
		lynx_parse_whitespace(&c);
		if (*c.json != '\0') {
			lynx_set_null(v);
			ret = LYNX_PARSE_ROOT_NOT_SINGULAR;
		}
	}
	assert(c.top == 0);	//栈中不能有残留
	free(c.stack);
	return ret;
	
}

lynx_type lynx_get_type(const lynx_value* v)
{
	return v->type;
}

double lynx_get_number(const lynx_value* v)
{
	assert(v != NULL && v->type == LYNX_NUMBER);
	return v->u.n;
}
void lynx_set_number(lynx_value* v, double n)
{
	assert(v);
	lynx_free(v);
	v->u.n = n;
	v->type = LYNX_NUMBER;
}

void lynx_free(lynx_value* v)
{
	assert(v);
	switch(v->type) {
		case LYNX_STRING:
			free(v->u.s.s);
			break;
		case LYNX_ARRAY:
			if (lynx_get_array_size(v) > 0) {
				for (size_t i = 0; i < lynx_get_array_size(v); ++i)
					lynx_free(lynx_get_array_element(v, i));
				free(v->u.a.e);
			}
			break;
		case LYNX_OBJECT:
			if (lynx_get_object_size(v) > 0) {
				for (size_t i = 0; i < lynx_get_object_size(v); ++i) {
					free(v->u.o.m[i].k);
					lynx_free(&(v->u.o.m[i].v));
				}
				free(v->u.o.m);
			}
			break;
		default: break;
	}
	v->type = LYNX_NULL;
}


static void lynx_set_string_raw(char** rs, size_t* rlen, const char* s, size_t len)
{
	assert(s || len == 0);
	memcpy(*rs = (char*)malloc(len + 1), s, len);
	(*rs)[len] = '\0';
	*rlen = len;
}

void lynx_set_string(lynx_value* v, const char* s, size_t len)
{
	assert(v);
	lynx_free(v);
	lynx_set_string_raw(&(v->u.s.s), &(v->u.s.len), s, len);
	v->type = LYNX_STRING;
}

size_t lynx_get_string_length(const lynx_value* v)
{
	assert(v && v->type == LYNX_STRING);
	return v->u.s.len;
}

const char* lynx_get_string(const lynx_value* v)
{
	assert(v && v->type == LYNX_STRING);
	return v->u.s.s;
}

int lynx_get_boolean(const lynx_value* v)
{
	assert(v && (v->type == LYNX_TRUE || v->type == LYNX_FALSE));
	return v->type == LYNX_TRUE;
}

void lynx_set_boolean(lynx_value* v, int b)
{
	assert(v);
	lynx_free(v);
	v->type = b ? LYNX_TRUE : LYNX_FALSE;
}

size_t lynx_get_array_size(const lynx_value* v)
{
	assert(v && v->type == LYNX_ARRAY);
	return v->u.a.size;
}

lynx_value* lynx_get_array_element(const lynx_value* v, size_t index)
{
	assert(v && v->type == LYNX_ARRAY && index < v->u.a.size);
	return v->u.a.e + index;
}

size_t lynx_get_object_size(const lynx_value* v)
{
	assert(v && v->type == LYNX_OBJECT);
	return v->u.o.size;
}

const char* lynx_get_object_key(const lynx_value* v, size_t index)
{
	assert(v && v->type == LYNX_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].k;
}

size_t lynx_get_object_key_length(const lynx_value* v, size_t index)
{
	assert(v && v->type == LYNX_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].klen;
}

lynx_value* lynx_get_object_value(const lynx_value* v, size_t index)
{
	assert(v && v->type == LYNX_OBJECT);
	assert(index < v->u.o.size);
	return &(v->u.o.m[index].v);
}

#ifndef LYNX_PARSE_STRINGIFY_INIT_SIZE
#define LYNX_PARSE_STRINGIFY_INIT_SIZE (1 << 8)
#endif
#define PUTS(c, s, len) memcpy(lynx_context_push(c, len), s, len)

static int lynx_stringify_string(lynx_context* c, const char* s, size_t len)
{
	assert(s);
	PUTC(c, '\"');
	for (int i = 0; i < len; ++i) {
		switch (s[i]) {
			case '\"': PUTS(c, "\\\"", 2); break;
			case '\\': PUTS(c, "\\\\", 2); break;
			case '\b': PUTS(c, "\\b", 2);  break;
			case '\f': PUTS(c, "\\f", 2);  break;
			case '\n': PUTS(c, "\\n", 2);  break;
			case '\r': PUTS(c, "\\r", 2);  break;
			case '\t': PUTS(c, "\\t", 2);  break;
			default:
				if (s[i] >= 0x20) {
					PUTC(c, s[i]);
				} else {
					char* buf = lynx_context_push(c, 7);
					sprintf(buf, "\\u%04X", s[i]);
					--c->top;
				}
				break;
		}
	}
	PUTC(c, '\"');
}

#define lynx_stringify_member(c, v, i) do { lynx_stringify_string((c), (v)->u.o.m[i].k, v->u.o.m[i].klen);\
	PUTC((c), ':'); lynx_stringify_value((c), &((v)->u.o.m[i].v)); } while (0)

static int lynx_stringify_value(lynx_context* c, const lynx_value* v)
{
	switch (v->type) {
		case LYNX_NULL:
			PUTS(c, "null", 4);
			break;
		case LYNX_TRUE:
			PUTS(c, "true", 4);
			break;
		case LYNX_FALSE:
			PUTS(c, "false", 5);
			break;
		case LYNX_NUMBER:{
			char* buffer = lynx_context_push(c, 32);
			int len = sprintf(buffer, "%.17g", v->u.n);
			c->top -= 32 - len;
			break;
		}
		case LYNX_STRING:
			lynx_stringify_string(c, v->u.s.s, v->u.s.len);
			break;
		case LYNX_ARRAY:
			PUTC(c, '[');
			if (v->u.a.size > 0) {
				lynx_stringify_value(c, &(v->u.a.e[0]));
				for (int i = 1; i < v->u.a.size; ++i) {
					PUTC(c, ',');
					lynx_stringify_value(c, &(v->u.a.e[i]));
				}
			}
			PUTC(c, ']');
			break;
		case LYNX_OBJECT:
			PUTC(c, '{');
			if (v->u.o.size > 0) {
				lynx_stringify_member(c, v, 0);
				for (int i = 1; i < v->u.o.size; ++i) {
					PUTC(c, ',');
					lynx_stringify_member(c, v, i);
				}
			}
			PUTC(c, '}');
			break;
		default:
			return LYNX_STRINGIFY_ERROR;
			break;
	}
	return LYNX_STRINGIFY_OK;
}

int lynx_stringify(const lynx_value* v, char** json, size_t* length)
{
	assert(v);
	assert(json);
	lynx_context c;
	int ret;
	c.stack = (char*)malloc(c.size = LYNX_PARSE_STRINGIFY_INIT_SIZE);
	c.top = 0;
	if ((ret = lynx_stringify_value(&c, v)) != LYNX_STRINGIFY_OK) {
		free(c.stack);
		*json = NULL;
		return ret;
	}
	if (length) *length = c.top;
	PUTC(&c, '\0');
	*json = c.stack;
	return LYNX_STRINGIFY_OK;
}

size_t lynx_find_object_index(const lynx_value* v, const char* key, size_t klen)
{
	assert(v && (v->type == LYNX_OBJECT) && key);
	for (size_t i = 0; i < v->u.o.size; ++i) {
		if (v->u.o.m[i].klen == klen && memcmp(v->u.o.m[i].k, key, klen) == 0)
			return i;
	}
	return LYNX_KEY_NOT_EXIST;
}

lynx_value* lynx_find_object_value(const lynx_value* v, const char* key, size_t klen)
{
	size_t index = lynx_find_object_index(v, key, klen);
	return index != LYNX_KEY_NOT_EXIST ? &(v->u.o.m[index].v) : NULL;
}

int lynx_is_equal(const lynx_value* lhs, const lynx_value* rhs)
{
	assert(lhs && rhs);
	if (lhs->type != rhs->type) return 0;
	switch (lhs->type) {
		case LYNX_STRING:
			if (lhs->u.s.len != rhs->u.s.len) return 0;
			return !memcmp(lhs->u.s.s, rhs->u.s.s, lhs->u.s.len);
		case LYNX_ARRAY:
			if (lhs->u.a.size != rhs->u.a.size) return 0;
			for (size_t i = 0; i < lhs->u.a.size; ++i) {
				if (!lynx_is_equal(&(lhs->u.a.e[i]), &(rhs->u.a.e[i])))
					return 0;
			}
			return 1;
			break;
		case LYNX_OBJECT:	//由于对象成员在概念上是无序的，不能简单的顺序比较,在这里使用简单的算法（大量的性能消耗）
			if (lhs->u.o.size != rhs->u.o.size) return 0;
			for (size_t i = 0; i < lhs->u.o.size; ++i) {
				lynx_value* rv = lynx_find_object_value(rhs, lhs->u.o.m[i].k, lhs->u.o.m[i].klen);
				if (!rv) return 0;
				if (!lynx_is_equal(&(lhs->u.o.m[i].v), rv)) return 0;
			}
			return 1;
			break;
		case LYNX_NUMBER:
			return fabs(lhs->u.n - rhs->u.n) < 1E-18;	//?
		default:
			return 1;
	}
}

void lynx_copy(lynx_value* dst, const lynx_value* src)
{
	assert(dst && src && dst != src);
	lynx_free(dst);
	switch (src->type) {
		case LYNX_STRING:
			lynx_set_string(dst, src->u.s.s, src->u.s.len);
			break;
		case LYNX_ARRAY:
			dst->type = LYNX_ARRAY;
			dst->u.a.size = src->u.a.size;
			dst->u.a.e = (lynx_value*)malloc(sizeof(lynx_value) * dst->u.a.size);
			for (size_t i = 0; i < dst->u.a.size; ++i) {
				lynx_copy(&(dst->u.a.e[i]), &(src->u.a.e[i]));
			}
			break;
		case LYNX_OBJECT:
			dst->type = LYNX_OBJECT;
			dst->u.o.size = dst->u.o.size;
			dst->u.o.m = (lynx_member*)malloc(sizeof(lynx_member) * dst->u.o.size);
			for (size_t i = 0; i < dst->u.o.size; ++i) {
				lynx_set_string_raw(&(dst->u.o.m[i].k), &(dst->u.o.m[i].klen), src->u.o.m[i].k, src->u.o.m[i].klen);
				lynx_copy(&(dst->u.o.m[i].v), &(src->u.o.m[i].v));
			}
			break;
		default:
			memcpy(dst, src, sizeof(lynx_value));
	}
}

void lynx_move(lynx_value* dst, lynx_value* src)
{
	if (!dst) {
		int a = 0;
	}
	if (!src) {
		int b = 0;
	}
	assert(dst && src);
	assert(dst != src);
	lynx_free(dst);
	memcpy(dst, src, sizeof(lynx_value));
	lynx_init(src);
}

void lynx_swap(lynx_value* lhs, lynx_value* rhs)
{
	assert(lhs && rhs);
	if (lhs == rhs) return;
	lynx_value tmp;
	memcpy(&tmp, lhs, sizeof(lynx_value));
	memcpy(lhs, rhs, sizeof(lynx_value));
	memcpy(rhs, &tmp, sizeof(lynx_value));
}

void lynx_set_array(lynx_value* v, size_t capacity)
{
	assert(v);
	lynx_free(v);
	v->type = LYNX_ARRAY;
	v->u.a.size = 0;
	v->u.a.capacity = capacity;
	v->u.a.e = capacity > 0 ? (lynx_value*)malloc(sizeof(lynx_value) * capacity) : NULL;
}

size_t lynx_get_array_capacity(const lynx_value* v)
{
	assert(v && v->type == LYNX_ARRAY);
	return v->u.a.capacity;
}

void lynx_reserve_array(lynx_value* v, size_t capacity)
{
	assert(v && v->type == LYNX_ARRAY);
	if (capacity <= v->u.a.capacity) return;
	v->u.a.e = (lynx_value*)realloc(v->u.a.e, capacity * sizeof(lynx_value));
	v->u.a.capacity = capacity;
}

void lynx_shrink_array(lynx_value* v)
{
	assert(v && v->type == LYNX_ARRAY);
	if (v->u.a.capacity > v->u.a.size) {
		v->u.a.capacity = v->u.a.size;
		v->u.a.e = (lynx_value*)realloc(v->u.a.e, v->u.a.size * sizeof(lynx_value));
	}
}

lynx_value* lynx_pushback_array_element(lynx_value* v)
{
	assert(v && v->type == LYNX_ARRAY);
	if (v->u.a.size == v->u.a.capacity) {
		lynx_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
	}
	lynx_init(&(v->u.a.e[v->u.a.size]));
	return &(v->u.a.e[v->u.a.size++]);
}

void lynx_popback_array_element(lynx_value* v)
{
	assert(v && v->type == LYNX_ARRAY && v->u.a.size > 0);
	lynx_free(&(v->u.a.e[--v->u.a.size]));
}

void lynx_clear_array(lynx_value* v)
{
	assert(v && v->type == LYNX_ARRAY);
	for (size_t i = 0; i < v->u.a.size; ++i) {
		lynx_free(&(v->u.a.e[i]));
	}
	v->u.a.size = 0;
}

lynx_value* lynx_insert_array_element(lynx_value* v, size_t index)
{
	assert(v && v->type == LYNX_ARRAY);
	assert(index <= v->u.a.size);
	lynx_pushback_array_element(v);
	for (size_t i = v->u.a.size - 1; i > index; --i) {
		memcpy(&(v->u.a.e[i]), &(v->u.a.e[i-1]), sizeof(lynx_value));
	}
	lynx_init(&(v->u.a.e[index]));
	return &(v->u.a.e[index]);
}

void lynx_erase_array_element(lynx_value* v, size_t index, size_t count)
{
	assert(v && v->type == LYNX_ARRAY);
	assert(index + count <= v->u.a.size);
	if (count == 0) return;
	for (size_t i = index; i < index + count; ++i) {
		lynx_free(&(v->u.a.e[i]));
	}
	for (size_t i = index + count; i < v->u.a.size; ++i) {
		memcpy(&(v->u.a.e[i - count]), &(v->u.a.e[i]), sizeof(lynx_value));
		lynx_init(&(v->u.a.e[i]));
	}
	v->u.a.size -= count;
}

size_t lynx_get_object_capacity(const lynx_value* v)
{
	assert(v && v->type == LYNX_OBJECT);
	return v->u.o.capacity;
}

void lynx_set_object(lynx_value* v, size_t capacity)
{
	assert(v);
	lynx_free(v);
	v->type = LYNX_OBJECT;
	v->u.o.size = 0;
	v->u.o.capacity = capacity;
	v->u.o.m = capacity > 0 ? (lynx_member*)malloc(sizeof(lynx_member) * capacity) : NULL;
}

void lynx_reserve_object(lynx_value* v, size_t capacity)
{
	assert(v && v->type == LYNX_OBJECT);
	if (capacity <= v->u.o.capacity) return;
	v->u.o.m = (lynx_member*)realloc(v->u.o.m, capacity * sizeof(lynx_member));
	v->u.o.capacity = capacity;
}

void lynx_shrink_object(lynx_value* v)
{
	assert(v && v->type == LYNX_OBJECT);
	if (v->u.o.capacity > v->u.o.size) {
		v->u.o.capacity = v->u.o.size;
		v->u.o.m = (lynx_member*)realloc(v->u.o.m, v->u.o.size * sizeof(lynx_member));
	}
}

void lynx_remove_object_value(lynx_value* v, size_t index)
{
	assert(v && v->type == LYNX_OBJECT);
	assert(index < v->u.o.size);
	free(v->u.o.m[index].k);
	lynx_free(&(v->u.o.m[index].v));
	for (size_t i = index + 1; i < v->u.o.size; ++i) {
		memcpy(&(v->u.o.m[i-1]), &(v->u.o.m[i]), sizeof(lynx_member));
	}
	--v->u.o.size;
}

lynx_value* lynx_set_object_value(lynx_value* v, const char* key, size_t klen)
{
	assert(v && v->type == LYNX_OBJECT && key);
	lynx_value* ret = lynx_find_object_value(v, key, klen);
	if (ret) return ret;
	if (v->u.o.size == v->u.o.capacity) {
		lynx_reserve_object(v, v->u.o.capacity == 0 ? 1 : v->u.o.capacity * 2);
	}
	lynx_member* cur = &(v->u.o.m[v->u.o.size]);
	lynx_set_string_raw(&(cur->k), &(cur->klen), key, klen);
	lynx_init(&(cur->v));
	++v->u.o.size;
	return &(cur->v);
}

void lynx_clear_object(lynx_value* v)
{
	assert(v && v->type == LYNX_OBJECT);
	for (size_t i = 0; i < v->u.o.size; ++i) {
		free(v->u.o.m[i].k);
		lynx_free(&(v->u.o.m[i].v));
	}
	v->u.o.size = 0;
}