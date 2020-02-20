#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lynxjson.h"

static int main_ret = 0;
static int test_count = 0;
static int test_pass = 0;

//宏技巧，如果宏中含有多个语句，需要使用do {/*...*/} while(0)包裹起来
#define EXPECT_EQ_BASE(equality, expect, actual, format) \
	do {\
		test_count++;\
		if (equality)\
			test_pass++;\
		else {\
			fprintf(stderr, "%s:%d: expect: " format " actual: " format "\n", __FILE__, __LINE__, expect, actual);\
			main_ret = 1;\
		}\
	} while(0)

#define EXPECT_EQ_INT(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%d")
#define EXPECT_EQ_DOUBLE(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%f")
#define EXPECT_EQ_STRING(expect, actuals, actuallen) EXPECT_EQ_BASE(sizeof(expect) - 1 == actuallen && memcmp(expect, actuals, actuallen) == 0, expect, actuals, "%s")

#if defined(_MSC_VER)
#define EXPECT_EQ_SIZE_T(expect, actual) EXPECT_EQ_BASE((expect) == (actual), (size_t)expect, (size_t)actual, "%Iu")
#else
#define EXPECT_EQ_SIZE_T(expect, actual) EXPECT_EQ_BASE((expect) == (actual), (size_t)expect, (size_t)actual, "%I64u")
#endif

#define EXPECT_TRUE(actual) EXPECT_EQ_BASE(actual, "true", "false", "%s")
#define EXPECT_FALSE(actual) EXPECT_EQ_BASE(!actual, "false", "true", "%s")
#define TEST_ERROR(error, json)\
	do {\
		lynx_value v;\
		v.type = LYNX_FALSE;\
		EXPECT_EQ_INT(error, lynx_parse(&v, json));\
		EXPECT_EQ_INT(LYNX_NULL, lynx_get_type(&v));\
	} while(0)

#define TEST_NUMBER(expect, json)\
	do {\
		lynx_value v;\
		EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, json));\
		EXPECT_EQ_INT(LYNX_NUMBER, lynx_get_type(&v));\
		EXPECT_EQ_DOUBLE(expect, lynx_get_number(&v));\
	} while(0)


#define TEST_STRING(expect, json)\
	do {\
		lynx_value v;\
		lynx_init(&v);\
		EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, json));\
		EXPECT_EQ_INT(LYNX_STRING, lynx_get_type(&v));\
		EXPECT_EQ_STRING(expect, lynx_get_string(&v), lynx_get_string_length(&v));\
	} while (0)

#define TEST_ROUNDTRIP(json)\
	do {\
		lynx_value v;\
		char* json2;\
		size_t len;\
		lynx_init(&v);\
		EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, json));\
		EXPECT_EQ_INT(LYNX_STRINGIFY_OK, lynx_stringify(&v, &json2, &len));\
		EXPECT_EQ_STRING(json, json2, len);\
		lynx_free(&v);\
		free(json2);\
	} while (0)

static void test_stringify()
{
	TEST_ROUNDTRIP("null");
	TEST_ROUNDTRIP("true");
	TEST_ROUNDTRIP("false");

    TEST_ROUNDTRIP("0");
    TEST_ROUNDTRIP("-0");
    TEST_ROUNDTRIP("1");
    TEST_ROUNDTRIP("-1");
    TEST_ROUNDTRIP("1.5");
    TEST_ROUNDTRIP("-1.5");
    TEST_ROUNDTRIP("3.25");
#if 1
    TEST_ROUNDTRIP("1e+020");
    TEST_ROUNDTRIP("1.234e+020");
    TEST_ROUNDTRIP("1.234e-020");
    TEST_ROUNDTRIP("1.0000000000000002");
    TEST_ROUNDTRIP("4.9406564584124654e-324"); /* minimum denormal */
    TEST_ROUNDTRIP("-4.9406564584124654e-324");
    TEST_ROUNDTRIP("2.2250738585072009e-308");  /* Max subnormal double */
    TEST_ROUNDTRIP("-2.2250738585072009e-308");
    TEST_ROUNDTRIP("2.2250738585072014e-308");  /* Min normal positive double */
    TEST_ROUNDTRIP("-2.2250738585072014e-308");
    TEST_ROUNDTRIP("1.7976931348623157e+308");  /* Max double */
    TEST_ROUNDTRIP("-1.7976931348623157e+308");
#endif

    TEST_ROUNDTRIP("\"\"");
    TEST_ROUNDTRIP("\"Hello\"");
    TEST_ROUNDTRIP("\"Hello\\nWorld\"");
    TEST_ROUNDTRIP("\"\\\" \\\\ / \\b \\f \\n \\r \\t\"");
    TEST_ROUNDTRIP("\"Hello\\u0000World\"");

    TEST_ROUNDTRIP("[]");
    TEST_ROUNDTRIP("[null,false,true,123,\"abc\",[1,2,3]]");

    TEST_ROUNDTRIP("{}");
    TEST_ROUNDTRIP("{\"n\":null,\"f\":false,\"t\":true,\"i\":123,\"s\":\"abc\",\"a\":[1,2,3],\"o\":{\"1\":1,\"2\":2,\"3\":3}}");
}

static void test_parse_null()
{
	lynx_value v;
	v.type = LYNX_NULL;
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, "null"));
	EXPECT_EQ_INT(LYNX_NULL, lynx_get_type(&v));
}

static void test_parse_true()
{
	lynx_value v;
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, " true "));
	EXPECT_EQ_INT(LYNX_TRUE, lynx_get_type(&v));
	EXPECT_EQ_INT(1, lynx_get_boolean(&v));
}

static void test_parse_false()
{
	lynx_value v;
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, "false"));
	EXPECT_EQ_INT(LYNX_FALSE, lynx_get_type(&v));
	EXPECT_EQ_INT(0, lynx_get_boolean(&v));
}

static void test_parse_number()
{
	TEST_NUMBER(0.0, "0");
	TEST_NUMBER(0.0, "-0");
	TEST_NUMBER(0.0, "-0.0");
	TEST_NUMBER(1.0, "1");
	TEST_NUMBER(-1.0, "-1");
	TEST_NUMBER(1.5, "1.5");
	TEST_NUMBER(-1.5, "-1.5");
	TEST_NUMBER(3.1416, "3.1416");
	TEST_NUMBER(1E10, "1E10");
	TEST_NUMBER(1e10, "1e10");
	TEST_NUMBER(1E+10, "1E+10");
	TEST_NUMBER(1E-10, "1E-10");
	TEST_NUMBER(-1E10, "-1E10");
	TEST_NUMBER(-1e10, "-1e10");
	TEST_NUMBER(-1E+10, "-1E+10");
	TEST_NUMBER(-1E-10, "-1E-10");
	TEST_NUMBER(1.234E+10, "1.234E+10");
	TEST_NUMBER(1.234E-10, "1.234E-10");
	TEST_NUMBER(0.0, "1e-10000");
	TEST_NUMBER(1.7976931348623157E308, "1.7976931348623157E308");
	TEST_NUMBER(-1.7976931348623157E308, "-1.7976931348623157E308");
	TEST_NUMBER(1.0000000000000002, "1.0000000000000002");
	TEST_NUMBER( 4.9406564584124654e-324, "4.9406564584124654e-324");
	TEST_NUMBER(-4.9406564584124654e-324, "-4.9406564584124654e-324");
	TEST_NUMBER( 2.2250738585072009e-308, "2.2250738585072009e-308");
	TEST_NUMBER(-2.2250738585072009e-308, "-2.2250738585072009e-308");
	TEST_NUMBER( 2.2250738585072014e-308, "2.2250738585072014e-308");
	TEST_NUMBER(-2.2250738585072014e-308, "-2.2250738585072014e-308");
}

static void test_parse_string()
{
	TEST_STRING("", "\"\"");
	TEST_STRING("Hello", "\"Hello\"");
	TEST_STRING("Hello\nWorld", "\"Hello\\nWorld\"");
	TEST_STRING("\"\\/\b\f\n\r\t", "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"");
	TEST_STRING("Hello\0World", "\"Hello\\u0000World\"");
	TEST_STRING("\x24", "\"\\u0024\"");
	TEST_STRING("\xC2\xA2", "\"\\u00A2\"");
	TEST_STRING("\xE2\x82\xAC", "\"\\u20AC\"");
	TEST_STRING("\xF0\x9D\x84\x9E", "\"\\uD834\\uDD1E\"");
	TEST_STRING("\xF0\x9D\x84\x9E", "\"\\ud834\\udd1e\"");
}

static void test_parse_array()
{
	lynx_value v;

	lynx_init(&v);
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, "[ ]"));
	EXPECT_EQ_INT(LYNX_ARRAY, lynx_get_type(&v));
	EXPECT_EQ_SIZE_T(0, lynx_get_array_size(&v));
	lynx_free(&v);

	lynx_init(&v);
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, "[ null, false, true, 123, \"abc\" ]"));
	EXPECT_EQ_INT(LYNX_ARRAY, lynx_get_type(&v));
	EXPECT_EQ_SIZE_T(5, lynx_get_array_size(&v));
	EXPECT_EQ_INT(LYNX_NULL, lynx_get_type(lynx_get_array_element(&v, 0)));
	EXPECT_EQ_INT(LYNX_FALSE, lynx_get_type(lynx_get_array_element(&v, 1)));
	EXPECT_EQ_INT(LYNX_TRUE, lynx_get_type(lynx_get_array_element(&v, 2)));
	EXPECT_EQ_INT(LYNX_NUMBER, lynx_get_type(lynx_get_array_element(&v, 3)));
	EXPECT_EQ_INT(LYNX_STRING, lynx_get_type(lynx_get_array_element(&v, 4)));
	EXPECT_EQ_DOUBLE(123.0, lynx_get_number(lynx_get_array_element(&v, 3)));
	EXPECT_EQ_STRING("abc", lynx_get_string(lynx_get_array_element(&v, 4)), lynx_get_string_length(lynx_get_array_element(&v, 4)));
	lynx_free(&v);

	lynx_init(&v);
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, "[ [ ], [ 0 ], [ 0, 1 ], [ 0, 1, 2 ] ]"));
	EXPECT_EQ_INT(LYNX_ARRAY, lynx_get_type(&v));
	EXPECT_EQ_SIZE_T(4, lynx_get_array_size(&v));
	for (size_t i = 0; i < 4; ++i) {
		lynx_value* a = lynx_get_array_element(&v, i);
		EXPECT_EQ_INT(LYNX_ARRAY, lynx_get_type(a));
		EXPECT_EQ_SIZE_T(i, lynx_get_array_size(a));
		for (size_t j = 0; j < i; ++j) {
			lynx_value* e = lynx_get_array_element(a, j);
			EXPECT_EQ_INT(LYNX_NUMBER, lynx_get_type(e));
			EXPECT_EQ_DOUBLE((double)j, lynx_get_number(e));
		}
	}
	lynx_free(&v);
}

static void test_parse_object()
{
	lynx_value v;
	lynx_init(&v);
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v, "{\n}\n"));
	EXPECT_EQ_INT(LYNX_OBJECT, lynx_get_type(&v));
	EXPECT_EQ_SIZE_T(0, lynx_get_object_size(&v));
	lynx_free(&v);

	lynx_init(&v);
	EXPECT_EQ_INT(LYNX_PARSE_OK, lynx_parse(&v,
		"{"
			"\"n\": null, "
			"\"f\": false, "
			"\"t\": true, "
			"\"i\": 3.14, "
			"\"s\": \"json\", "
			"\"a\": [1, 2, 3], "
			"\"o\": {\"1\":1,\"2\":2,\"3\":3}"
		"}"
	));
	EXPECT_EQ_INT(LYNX_OBJECT, lynx_get_type(&v));
	EXPECT_EQ_SIZE_T(7, lynx_get_object_size(&v));
    EXPECT_EQ_STRING("n", lynx_get_object_key(&v, 0), lynx_get_object_key_length(&v, 0));
    EXPECT_EQ_INT(LYNX_NULL,   lynx_get_type(lynx_get_object_value(&v, 0)));
    EXPECT_EQ_STRING("f", lynx_get_object_key(&v, 1), lynx_get_object_key_length(&v, 1));
    EXPECT_EQ_INT(LYNX_FALSE,  lynx_get_type(lynx_get_object_value(&v, 1)));
    EXPECT_EQ_STRING("t", lynx_get_object_key(&v, 2), lynx_get_object_key_length(&v, 2));
    EXPECT_EQ_INT(LYNX_TRUE,   lynx_get_type(lynx_get_object_value(&v, 2)));
    EXPECT_EQ_STRING("i", lynx_get_object_key(&v, 3), lynx_get_object_key_length(&v, 3));
    EXPECT_EQ_INT(LYNX_NUMBER, lynx_get_type(lynx_get_object_value(&v, 3)));
    EXPECT_EQ_DOUBLE(3.14, lynx_get_number(lynx_get_object_value(&v, 3)));
    EXPECT_EQ_STRING("s", lynx_get_object_key(&v, 4), lynx_get_object_key_length(&v, 4));
    EXPECT_EQ_INT(LYNX_STRING, lynx_get_type(lynx_get_object_value(&v, 4)));
    EXPECT_EQ_STRING("json", lynx_get_string(lynx_get_object_value(&v, 4)), lynx_get_string_length(lynx_get_object_value(&v, 4)));
    EXPECT_EQ_STRING("a", lynx_get_object_key(&v, 5), lynx_get_object_key_length(&v, 5));
    EXPECT_EQ_INT(LYNX_ARRAY, lynx_get_type(lynx_get_object_value(&v, 5)));
    EXPECT_EQ_SIZE_T(3, lynx_get_array_size(lynx_get_object_value(&v, 5)));
    for (size_t i = 0; i < 3; i++) {
        lynx_value* e = lynx_get_array_element(lynx_get_object_value(&v, 5), i);
        EXPECT_EQ_INT(LYNX_NUMBER, lynx_get_type(e));
        EXPECT_EQ_DOUBLE(i + 1.0, lynx_get_number(e));
    }
    EXPECT_EQ_STRING("o", lynx_get_object_key(&v, 6), lynx_get_object_key_length(&v, 6));
    {
        lynx_value* o = lynx_get_object_value(&v, 6);
        EXPECT_EQ_INT(LYNX_OBJECT, lynx_get_type(o));
        for (size_t i = 0; i < 3; i++) {
            lynx_value* ov = lynx_get_object_value(o, i);
            EXPECT_TRUE('1' + i == lynx_get_object_key(o, i)[0]);
            EXPECT_EQ_SIZE_T(1, lynx_get_object_key_length(o, i));
            EXPECT_EQ_INT(LYNX_NUMBER, lynx_get_type(ov));
            EXPECT_EQ_DOUBLE(i + 1.0, lynx_get_number(ov));
        }
    }
    lynx_free(&v);
}

static void test_parse_expect_value()
{
	TEST_ERROR(LYNX_PARSE_EXPECT_VALUE, "");
	TEST_ERROR(LYNX_PARSE_EXPECT_VALUE, "  ");
	TEST_ERROR(LYNX_PARSE_EXPECT_VALUE, "\n  \r  \t");
	TEST_ERROR(LYNX_PARSE_EXPECT_VALUE, "\n\n\n\n");
}

static void test_parse_invalid_value()
{
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "+0"); //数字前面不能有+号
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "+1");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, ".123"); //.之前至少一位数字
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "1.");   //.之后至少一位数字
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "INF");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "inf");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "NAN");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "nan");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "ture");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "flase");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "nuII");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "TRUE");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "FALSE");
	TEST_ERROR(LYNX_PARSE_INVALID_VALUE, "[1, 2, 3, ]");    //逗号后面必须有值
}

static void test_parse_root_not_singular()
{
	TEST_ERROR(LYNX_PARSE_ROOT_NOT_SINGULAR, "null x");

	//数字格式不合规范
	TEST_ERROR(LYNX_PARSE_ROOT_NOT_SINGULAR, "0xff");
	TEST_ERROR(LYNX_PARSE_ROOT_NOT_SINGULAR, "0b110");
	TEST_ERROR(LYNX_PARSE_ROOT_NOT_SINGULAR, "0777");  
}

static void test_parse_number_too_big()
{
	TEST_ERROR(LYNX_PARSE_NUMBER_TOO_BIG, "1.0e309");
	TEST_ERROR(LYNX_PARSE_NUMBER_TOO_BIG, "-2.0e309");
}

static void test_parse_missing_quotation_mark()
{
	TEST_ERROR(LYNX_PARSE_MISS_QUOTATION_MARK, "\"nihao");
}

static void test_parse_invalid_string_escape()
{
	TEST_ERROR(LYNX_PARSE_INVALID_STRING_ESCAPE, "\"\\v\"");
	TEST_ERROR(LYNX_PARSE_INVALID_STRING_ESCAPE, "\"\\'\"");
	TEST_ERROR(LYNX_PARSE_INVALID_STRING_ESCAPE, "\"\\0\"");
	TEST_ERROR(LYNX_PARSE_INVALID_STRING_ESCAPE, "\"\\x12\"");
}

static void test_parse_invalid_string_char()
{
	TEST_ERROR(LYNX_PARSE_INVALID_STRING_CHAR, "\"\x01\"");
	TEST_ERROR(LYNX_PARSE_INVALID_STRING_CHAR, "\"\x1F\"");
}

static void test_parse_invalid_unicode_hex()
{
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u0\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u01\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u012\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u/000\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\uG000\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u0/00\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u0G00\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u00/0\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u00G0\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u000/\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_HEX, "\"\\u000G\"");
}

static void test_parse_invalid_unicode_surrogate()
{
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uDBFF\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\\\\\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\\uDBFF\"");
	TEST_ERROR(LYNX_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\\uE000\"");
}

static void test_parse_miss_comma_or_square_bracket()
{
	TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_SQUARE_BRACKET, "[1, 2");
	TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_SQUARE_BRACKET, "[\"nihao\" 123]");
	TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_SQUARE_BRACKET, "[1, 2}");
	TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_SQUARE_BRACKET, "[[],[]");
}

static void test_parse_miss_colon()
{
	TEST_ERROR(LYNX_PARSE_MISS_COLON, "{\"1\"}");
	TEST_ERROR(LYNX_PARSE_MISS_COLON, "{\"1\": 2, \"3\"}");
    TEST_ERROR(LYNX_PARSE_MISS_COLON, "{\"a\"}");
    TEST_ERROR(LYNX_PARSE_MISS_COLON, "{\"a\",\"b\"}");
}

static void test_parse_miss_comma_or_curly_bracket()
{
	TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET, "{\"1\": 2 nihao");
	TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET, "{\"1\": 2, \"3\":4");
    TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET, "{\"a\":1");
    TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET, "{\"a\":1]");
    TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET, "{\"a\":1 \"b\"");
    TEST_ERROR(LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET, "{\"a\":{}");
}

static void test_parse_miss_key()
{
	TEST_ERROR(LYNX_PARSE_MISS_KEY, "{:2}");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{:1,");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{1:1,");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{true:1,");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{false:1,");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{null:1,");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{[]:1,");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{{}:1,");
    TEST_ERROR(LYNX_PARSE_MISS_KEY, "{\"a\":1,");
}

static void test_access_null()
{
	lynx_value v;
	lynx_init(&v);
	lynx_set_string(&v, "a", 1);
	lynx_set_null(&v);
	EXPECT_EQ_INT(LYNX_NULL, lynx_get_type(&v));
	lynx_free(&v);
}

static void test_access_string()
{
	lynx_value v;
	lynx_init(&v);
	lynx_set_string(&v, "", 0);
	EXPECT_EQ_STRING("", lynx_get_string(&v), lynx_get_string_length(&v));
	lynx_set_string(&v, "Hello", 5);
	EXPECT_EQ_STRING("Hello", lynx_get_string(&v), lynx_get_string_length(&v));
	lynx_free(&v);
}

static void test_access_boolean()
{
	lynx_value v;
	lynx_init(&v);
	lynx_set_string(&v, "a", 1);
	lynx_set_boolean(&v, 1);
	EXPECT_TRUE(lynx_get_boolean(&v));
	lynx_set_boolean(&v, 0);
	EXPECT_FALSE(lynx_get_boolean(&v));
	lynx_free(&v);
}

static void test_access_number()
{
	lynx_value v;
	lynx_init(&v);
	lynx_set_string(&v, "a", 1);
	lynx_set_number(&v, 123.4);
	EXPECT_EQ_DOUBLE(123.4, lynx_get_number(&v));
	lynx_free(&v);
}

static void test_access_array()
{
	lynx_value a, e;
	size_t i, j;
	lynx_init(&a);

	for (i = 0; i <= 5; i += 5) {
		lynx_set_array(&a, i);
		EXPECT_EQ_SIZE_T(0, lynx_get_array_size(&a));
		EXPECT_EQ_SIZE_T(i, lynx_get_array_capacity(&a));
		for (j = 0; j < 10; ++j) {
			lynx_init(&e);
			lynx_set_number(&e, j);
			lynx_move(lynx_pushback_array_element(&a), &e);
			lynx_free(&e);
		}
		EXPECT_EQ_SIZE_T(10, lynx_get_array_size(&a));
		for (j = 0; j < 10; ++j)
			EXPECT_EQ_DOUBLE((double)j, lynx_get_number(lynx_get_array_element(&a, j)));
	}

	lynx_popback_array_element(&a);
	EXPECT_EQ_SIZE_T(9, lynx_get_array_size(&a));
	for (i = 0; i < 9; ++i)
		EXPECT_EQ_DOUBLE((double)i, lynx_get_number(lynx_get_array_element(&a, i)));
	
	lynx_erase_array_element(&a, 4, 0);
	EXPECT_EQ_SIZE_T(9, lynx_get_array_size(&a));
	for (i = 0; i < 9; ++i)
		EXPECT_EQ_DOUBLE((double)i, lynx_get_number(lynx_get_array_element(&a, i)));

	lynx_erase_array_element(&a, 8, 1);
	EXPECT_EQ_SIZE_T(8, lynx_get_array_size(&a));
	for (i = 0; i < 8; ++i)
		EXPECT_EQ_DOUBLE((double)i, lynx_get_number(lynx_get_array_element(&a, i)));

	lynx_erase_array_element(&a, 0, 2);
	EXPECT_EQ_SIZE_T(6, lynx_get_array_size(&a));
	for (i = 0; i < 6; ++i)
		EXPECT_EQ_DOUBLE((double)i + 2, lynx_get_number(lynx_get_array_element(&a, i)));

	for (i = 0; i < 2; ++i) {
		lynx_init(&e);
		lynx_set_number(&e, i);
		lynx_move(lynx_insert_array_element(&a, i), &e);
		lynx_free(&e);
	}

	EXPECT_EQ_SIZE_T(8, lynx_get_array_size(&a));
	for (i = 0; i < 8; ++i) {
		EXPECT_EQ_DOUBLE((double)i, lynx_get_number(lynx_get_array_element(&a, i)));
	}

	lynx_set_string(&e, "Hello", 5);
	lynx_move(lynx_pushback_array_element(&a), &e);
	lynx_free(&e);

	i = lynx_get_array_capacity(&a);
	lynx_clear_array(&a);
	EXPECT_EQ_SIZE_T(0, lynx_get_array_size(&a));
	EXPECT_EQ_SIZE_T(i, lynx_get_array_capacity(&a));
	lynx_shrink_array(&a);
	EXPECT_EQ_SIZE_T(0, lynx_get_array_capacity(&a));

	lynx_free(&a);
}

static void test_access_object() {
    lynx_value o, v, *pv;
    size_t i, j, index;

    lynx_init(&o);

    for (j = 0; j <= 5; j += 5) {
        lynx_set_object(&o, j);
        EXPECT_EQ_SIZE_T(0, lynx_get_object_size(&o));
        EXPECT_EQ_SIZE_T(j, lynx_get_object_capacity(&o));
        for (i = 0; i < 10; i++) {
            char key[2] = "a";
            key[0] += i;
            lynx_init(&v);
            lynx_set_number(&v, i);
            lynx_move(lynx_set_object_value(&o, key, 1), &v);
            lynx_free(&v);
        }
        EXPECT_EQ_SIZE_T(10, lynx_get_object_size(&o));
        for (i = 0; i < 10; i++) {
            char key[] = "a";
            key[0] += i;
            index = lynx_find_object_index(&o, key, 1);
            EXPECT_TRUE(index != LYNX_KEY_NOT_EXIST);
            pv = lynx_get_object_value(&o, index);
            EXPECT_EQ_DOUBLE((double)i, lynx_get_number(pv));
        }
    }

    index = lynx_find_object_index(&o, "j", 1);    
    EXPECT_TRUE(index != LYNX_KEY_NOT_EXIST);
    lynx_remove_object_value(&o, index);
    index = lynx_find_object_index(&o, "j", 1);
    EXPECT_TRUE(index == LYNX_KEY_NOT_EXIST);
    EXPECT_EQ_SIZE_T(9, lynx_get_object_size(&o));

    index = lynx_find_object_index(&o, "a", 1);
    EXPECT_TRUE(index != LYNX_KEY_NOT_EXIST);
    lynx_remove_object_value(&o, index);
    index = lynx_find_object_index(&o, "a", 1);
    EXPECT_TRUE(index == LYNX_KEY_NOT_EXIST);
    EXPECT_EQ_SIZE_T(8, lynx_get_object_size(&o));

    EXPECT_TRUE(lynx_get_object_capacity(&o) > 8);
    lynx_shrink_object(&o);
    EXPECT_EQ_SIZE_T(8, lynx_get_object_capacity(&o));
    EXPECT_EQ_SIZE_T(8, lynx_get_object_size(&o));
    for (i = 0; i < 8; i++) {
        char key[] = "a";
        key[0] += i + 1;
        EXPECT_EQ_DOUBLE((double)i + 1, lynx_get_number(lynx_get_object_value(&o, lynx_find_object_index(&o, key, 1))));
    }

    lynx_set_string(&v, "Hello", 5);
    lynx_move(lynx_set_object_value(&o, "World", 5), &v); /* Test if element is freed */
    lynx_free(&v);

    pv = lynx_find_object_value(&o, "World", 5);
    EXPECT_TRUE(pv != NULL);
    EXPECT_EQ_STRING("Hello", lynx_get_string(pv), lynx_get_string_length(pv));

    i = lynx_get_object_capacity(&o);
    lynx_clear_object(&o);
    EXPECT_EQ_SIZE_T(0, lynx_get_object_size(&o));
    EXPECT_EQ_SIZE_T(i, lynx_get_object_capacity(&o)); /* capacity remains unchanged */
    lynx_shrink_object(&o);
    EXPECT_EQ_SIZE_T(0, lynx_get_object_capacity(&o));

    lynx_free(&o);
}

static void test_access()
{
	test_access_null();
	test_access_boolean();
	test_access_number();
	test_access_string();
	test_access_array();
	test_access_object();
}

static void test_parse()
{
	test_parse_null();
	test_parse_true();
	test_parse_false();
	test_parse_number();
	test_parse_string();
	test_parse_array();
	test_parse_object();
	test_parse_expect_value();
	test_parse_invalid_value();
	test_parse_root_not_singular();
	test_parse_number_too_big();
	test_parse_missing_quotation_mark();
	test_parse_invalid_string_escape();
	test_parse_invalid_string_char();
	test_parse_invalid_unicode_hex();
	test_parse_invalid_unicode_surrogate();
	test_parse_miss_comma_or_square_bracket();
	test_parse_miss_colon();
	test_parse_miss_comma_or_curly_bracket();
	test_parse_miss_key();
}

int main()
{
	test_parse();
	test_access();
	test_stringify();
	printf("%d/%d (%3.2f%%) passed\n", test_pass, test_count, test_pass * 100.0 / test_count);
	return main_ret;
}