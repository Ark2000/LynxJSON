#ifndef LYNXJSON_H__
#define LYNXJSON_H__
#include <stddef.h>	//size_t

//JSON值类型枚举
typedef enum LYNX_TYPE {
	LYNX_NULL,		//null
	LYNX_FALSE,		//布尔值，false		
	LYNX_TRUE,		//布尔值，true
	LYNX_NUMBER,	//double
	LYNX_STRING,	//JSON字符串
	LYNX_ARRAY,		//数组
	LYNX_OBJECT,	//对象（字典）
} lynx_type;

//JSON对象成员类型（键值对）
typedef struct lynx_member lynx_member;
//JSON值结构体
typedef struct lynx_value lynx_value;

struct lynx_value {
	lynx_type type;	//类型
	union {
		double n;											//LYNX_NUMBER
		struct { char* s; size_t len; }s;					//LYNX_STRING
		struct { lynx_value* e; size_t size, capacity; }a;	//LYNX_ARRAY
		struct { lynx_member* m; size_t size, capacity; }o;	//LYNX_OBJECT
	}u;
};

struct lynx_member {
	char* k; size_t klen;	//JSON字符串
	lynx_value v;
};

//lynx_parse的返回值/错误标记,详情参见test.c查看示例
enum LYNX_PARSE {
	LYNX_PARSE_OK = 0,              		//解析成功
	LYNX_PARSE_EXPECT_VALUE,        		//JSON文件仅含有空白, 如" \t\n\r"
	LYNX_PARSE_INVALID_VALUE,       		//值无法识别, 如"f654awf65""
	LYNX_PARSE_ROOT_NOT_SINGULAR,  	 		//一个合法值之后除了空白还有其他字符，如"nullx", "null x",
	LYNX_PARSE_NUMBER_TOO_BIG,       		//如1E1000
	LYNX_PARSE_MISS_QUOTATION_MARK,			//字符串引号缺失，如"nihao
	LYNX_PARSE_INVALID_STRING_ESCAPE,		//非法转义字符，如\x
	LYNX_PARSE_INVALID_STRING_CHAR,			//非法字符如\b
	LYNX_PARSE_INVALID_UNICODE_HEX,			//\u后不是四位十六进制数字
	LYNX_PARSE_INVALID_UNICODE_SURROGATE,	//Unicode只有高代理项缺少低代理项或低代理项非法
	LYNX_PARSE_MISS_COMMA_OR_SQUARE_BRACKET,//数组中缺失逗号或者右方括号
	LYNX_PARSE_MISS_COLON,					//对象中缺失冒号
	LYNX_PARSE_MISS_COMMA_OR_CURLY_BRACKET,	//对象中缺失右或括号或逗号
	LYNX_PARSE_MISS_KEY,					//对象中的键值对缺失值
};

enum LYNX_STRINGIFY {
	LYNX_STRINGIFY_OK,
	LYNX_STRINGIFY_ERROR,
};

//初始化节点（将节点的类型设为空）
#define lynx_init(v) do { (v)->type = LYNX_NULL; } while(0)

//解析JSON文本，存入用户提供的节点
int lynx_parse(lynx_value* v, const char* json);

//释放节点申请的资源（字符串，数组，对象），在更改节点的类型或销毁节点时必须调用，否则会造成内存泄漏
void lynx_free(lynx_value* v);

//获取节点的类型
lynx_type lynx_get_type(const lynx_value* v);

//比较两个节点内容是否一致
int lynx_is_equal(const lynx_value* lhs, const lynx_value* rhs);

//深拷贝
void lynx_copy(lynx_value* dst, const lynx_value* src);

//移动（资源转移）
void lynx_move(lynx_value* dst, lynx_value* src);

//交换
void lynx_swap(lynx_value* lhs, lynx_value* rhs);

//将节点类型设置为LYNX_NULL
#define lynx_set_null(v) lynx_free(v)

//获取节点的逻辑值
int lynx_get_boolean(const lynx_value* v);
//将节点的类型设置为布尔，同时提供值
void lynx_set_boolean(lynx_value* v, int b);

//获取节点的实数值
double lynx_get_number(const lynx_value* v);
//将节点的类型设置为LYNX_NUMBER，同时提供值
void lynx_set_number(lynx_value* v, double n);

//获取节点的字符串值
const char* lynx_get_string(const lynx_value* v);
//获取节点字符串长度
size_t lynx_get_string_length(const lynx_value* v);
//将节点类型设置为LYNX_STRING,同时提供JSON格式字符串和长度信息
//如s = "\\u0700\\u007F", len = 3
void lynx_set_string(lynx_value* v, const char* s, size_t len);

//动态数组相关
void lynx_set_array(lynx_value* v, size_t capacity);
void lynx_reserve_array(lynx_value* v, size_t capacity);
void lynx_shrink_array(lynx_value* v);
size_t lynx_get_array_size(const lynx_value* v);
size_t lynx_get_array_capacity(const lynx_value* v);
lynx_value* lynx_get_array_element(const lynx_value* v, size_t index);
lynx_value* lynx_pushback_array_element(lynx_value* v);
void lynx_popback_array_element(lynx_value* v);//清空数组所有元素（不改变容量）
lynx_value* lynx_insert_array_element(lynx_value* v, size_t index);
void lynx_erase_array_element(lynx_value* v, size_t index, size_t count);
void lynx_clear_array(lynx_value* v);

//对象(字典)相关
#define LYNX_KEY_NOT_EXIST ((size_t)-1)

void lynx_set_object(lynx_value* v, size_t capacity);
void lynx_reserve_object(lynx_value* v, size_t capacity);
void lynx_shrink_object(lynx_value* v);
size_t lynx_get_object_size(const lynx_value* v);
size_t lynx_get_object_capacity(const lynx_value* v);
const char* lynx_get_object_key(const lynx_value* v, size_t index);
size_t lynx_get_object_key_length(const lynx_value* v, size_t index);
lynx_value* lynx_get_object_value(const lynx_value* v, size_t index);
size_t lynx_find_object_index(const lynx_value* v, const char* key, size_t klen);
lynx_value* lynx_find_object_value(const lynx_value* v, const char* key, size_t klen);
void lynx_remove_object_value(lynx_value* v, size_t index);
lynx_value* lynx_set_object_value(lynx_value* v, const char* key, size_t klen);
void lynx_clear_object(lynx_value* v);

//将节点转为json文本，需要使用者自行free字符串
int lynx_stringify(const lynx_value* v, char** json, size_t* length);

#endif