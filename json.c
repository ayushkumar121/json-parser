#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>

void panic(char *message)
{
    fprintf(stderr, "panic: %s", message);
    abort();
}

#define ARENA_DEFAULT_CAP 1024

typedef struct Arena Arena;
typedef struct Arena
{
    size_t cap;
    size_t used;
    char *pool;
    Arena *next;
} Arena;

inline size_t align(size_t addr, size_t alignment)
{
    size_t rem = addr % alignment;
    if (rem == 0)
        return addr;
    else
        return (addr - rem) + alignment;
}

Arena *arena_new(size_t cap)
{
    cap = align(cap, 8);
    Arena *arena = malloc(sizeof(Arena));
    assert(arena != NULL);
    arena->cap = cap;
    arena->used = 0;
    arena->pool = malloc(cap);
    assert(arena->pool != NULL);
    arena->next = NULL;

    return arena;
}

inline Arena *arena_default_new()
{
    return arena_new(ARENA_DEFAULT_CAP);
}

void arena_free(Arena *arena)
{
    if (arena->pool != NULL)
        free(arena->pool);
    if (arena->next != NULL)
        arena_free(arena->next);

    free(arena);
}

void *arena_alloc(Arena *arena, size_t size)
{
    assert(arena != NULL);

    size = align(size, 8);
    if ((arena->used + size) < arena->cap)
    {
        void *ptr = arena->pool + arena->used;
        memset(ptr, 0, size);
        arena->used += size;
        return ptr;
    }
    else
    {
        if (arena->next == NULL)
        {
            arena->next = arena_new(arena->cap);
        }
        return arena_alloc(arena->next, size);
    }
}

Arena *DEFAULT_ARENA = NULL;

void *_new(size_t size)
{
    return arena_alloc(DEFAULT_ARENA, size);
}

#define new(T) _new(sizeof(T))

#define Array(T)    \
    struct          \
    {               \
        size_t len; \
        size_t cap; \
        T *ptr;     \
    }

#define Slice(T)    \
    struct          \
    {               \
        size_t len; \
        T *ptr;     \
    }

#define slice(arr, start, len) {.len = (len), .ptr = ((arr)->ptr + start)}

#define ARRAY_INIT_CAP 10

#define array_append(arr, item)                                                       \
    do                                                                                \
    {                                                                                 \
        if ((arr)->cap < (arr)->len + 1)                                              \
        {                                                                             \
            (arr)->cap =                                                              \
                ((arr)->cap == 0) ? ARRAY_INIT_CAP : (arr)->cap * 2;                  \
            void *dst = arena_alloc(DEFAULT_ARENA, (arr)->cap * sizeof(*(arr)->ptr)); \
            if ((arr)->len > 0)                                                       \
                memmove(dst, (arr)->ptr, (arr)->len);                                 \
            (arr)->ptr = dst;                                                         \
        }                                                                             \
        (arr)->ptr[(arr)->len++] = (item);                                            \
    } while (0)

typedef Array(char) String;
typedef Slice(char) StringView;

StringView sv_from_str(String s)
{
    return (StringView){.len = s.len, .ptr = s.ptr};
}

StringView sv_from_cstr(char *cstr, size_t len)
{
    return (StringView){.len = len, .ptr = cstr};
}

typedef enum
{
    JSON_NULL,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct
{
    JsonType type;
    void *value;
} JsonValue;

typedef String JsonString;
typedef double JsonNumber;
typedef Array(JsonValue) JsonArray;

typedef struct
{
    JsonString *key;
    JsonValue value;
} JsonObjectProperty;

typedef struct
{
    Array(JsonObjectProperty) properties;
} JsonObject;

void encode_json(FILE *stream, JsonValue root)
{
    switch (root.type)
    {
    case JSON_NULL:
    {
        fprintf(stream, "null");
    }
    break;

    case JSON_TRUE:
    {
        fprintf(stream, "true");
    }
    break;

    case JSON_FALSE:
    {
        fprintf(stream, "false");
    }
    break;

    case JSON_NUMBER:
    {
        fprintf(stream, "%g", *(double *)root.value);
    }
    break;

    case JSON_STRING:
    {
        JsonString *value = (JsonString *)root.value;
        fprintf(stream, "\"%.*s\"", (int)value->len, value->ptr);
    }
    break;

    case JSON_ARRAY:
    {
        JsonArray *value = (JsonArray *)root.value;
        fprintf(stream, "[ ");
        for (size_t i = 0; i < value->len; i++)
        {
            encode_json(stream, value->ptr[i]);
            if (i < value->len - 1)
                fprintf(stream, ",");
        }
        fprintf(stream, "] ");
    }
    break;

    case JSON_OBJECT:
    {
        JsonObject *value = (JsonObject *)root.value;
        fprintf(stream, "{ ");
        for (size_t i = 0; i < value->properties.len; i++)
        {
            JsonObjectProperty prop = value->properties.ptr[i];
            fprintf(stream, "\"%.*s\" : ", (int)prop.key->len, prop.key->ptr);
            encode_json(stream, prop.value);
            if (i < value->properties.len - 1)
                fprintf(stream, ",");
        }
        fprintf(stream, "} ");
    }
    break;
    default:
        panic("unreachable");
    }
}

inline bool is_whitespace(char c)
{
    return (c == '\n' || c == ' ' || c == '\t' || c == '\r');
}

inline bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

inline void consume_char(StringView *str)
{
    assert(str->len > 0);
    str->ptr++;
    str->len--;
}

void trim_left(StringView *str)
{
    while (str->len > 0 && is_whitespace(*str->ptr))
    {
        consume_char(str);
    }
}

bool consume_literal(StringView *str, char *literal, size_t literal_size)
{
    if (str->len < literal_size)
        return false;

    for (size_t i = 0; i < literal_size; i++)
    {
        if (*str->ptr != literal[i])
            return false;

        consume_char(str);
    }
    return true;
}

typedef struct
{
    bool error;
    JsonValue value;
} JsonResult;

#define ErrResult \
    (JsonResult) { .error = true }

#define OkResult(val) \
    (JsonResult) { .error = false, .value = val }

JsonResult decode_json_value(StringView *str);

JsonResult decode_json_null(StringView *str)
{
    if (!consume_literal(str, "null", 4))
        return ErrResult;

    JsonValue json_null = {0};
    json_null.type = JSON_NULL;

    return OkResult(json_null);
}

JsonResult decode_json_true(StringView *str)
{
    if (!consume_literal(str, "true", 4))
        return ErrResult;

    JsonValue json_true = {0};
    json_true.type = JSON_TRUE;

    return OkResult(json_true);
}

JsonResult decode_json_false(StringView *str)
{
    if (!consume_literal(str, "false", 5))
        return ErrResult;

    JsonValue json_false = {0};
    json_false.type = JSON_FALSE;

    return OkResult(json_false);
}

JsonResult decode_json_number(StringView *str)
{
    bool is_negative = false;
    if (*str->ptr == '-')
    {
        is_negative = true;
        consume_char(str);
    }

    double num = 0;
    int fraction_count = 1;
    bool fraction_part = false;

    while (str->len > 0)
    {
        char c = *str->ptr;
        if (c == ',' || c == ']' || c == '}' || is_whitespace(c))
            break;

        if (is_digit(c))
        {
            int d = c - '0';
            if (fraction_part)
            {
                num = num + pow(10, -fraction_count) * d;
                fraction_count++;
            }
            else
            {
                num = num * 10 + d;
            }
        }
        else if (c == '.' && !fraction_part)
        {
            fraction_part = true;
        }
        else
        {
            return ErrResult;
        }

        consume_char(str);
    }
    if (is_negative)
        num *= -1;

    JsonNumber *json_num_val = new (JsonNumber);
    *json_num_val = num;

    JsonValue json_num = {0};
    json_num.type = JSON_NUMBER;
    json_num.value = json_num_val;

    return OkResult(json_num);
}

JsonResult decode_json_string(StringView *str)
{
    if (*str->ptr != '\"')
        return ErrResult;
    consume_char(str);

    String *s = new (String);
    bool found_end = false;
    while (str->len > 0)
    {
        char c = *str->ptr;
        if (c == '\"')
        {
            found_end = true;
            consume_char(str);
            break;
        };

        array_append(s, c);
        consume_char(str);
    }
    if (!found_end)
        return ErrResult;

    JsonValue json_string = {0};
    json_string.type = JSON_STRING;
    json_string.value = s;

    return OkResult(json_string);
}

JsonResult decode_json_array(StringView *str)
{
    if (*str->ptr != '[')
        return ErrResult;
    consume_char(str);

    JsonArray *arr = new (JsonArray);
    while (str->len > 0)
    {
        trim_left(str);
        if (*str->ptr == ']')
            break;

        JsonResult res = decode_json_value(str);
        if (res.error)
            return ErrResult;

        array_append(arr, res.value);

        trim_left(str);
        if (*str->ptr != ',')
            break;
        consume_char(str);
    }

    if (*str->ptr != ']')
        return ErrResult;
    consume_char(str);

    JsonValue json_arr = {0};
    json_arr.type = JSON_ARRAY;
    json_arr.value = arr;

    return OkResult(json_arr);
}

JsonResult decode_json_object(StringView *str)
{
    if (*str->ptr != '{')
        return ErrResult;
    consume_char(str);

    JsonObject *object = new (JsonObject);
    while (str->len > 0)
    {
        trim_left(str);
        if (*str->ptr == '}')
            break;

        JsonObjectProperty property = {0};

        JsonResult res = ErrResult;
        res = decode_json_string(str);
        if (res.error)
            return ErrResult;

        property.key = res.value.value;

        if (*str->ptr != ':')
            return ErrResult;
        consume_char(str);

        trim_left(str);
        res = decode_json_value(str);
        if (res.error)
            return ErrResult;

        property.value = res.value;

        array_append(&object->properties, property);

        trim_left(str);
        if (*str->ptr != ',')
            break;
        consume_char(str);
    }

    if (*str->ptr != '}')
        return ErrResult;
    consume_char(str);

    JsonValue jsob_obj = {0};
    jsob_obj.type = JSON_OBJECT;
    jsob_obj.value = object;

    return OkResult(jsob_obj);
}

JsonResult decode_json_value(StringView *str)
{
    trim_left(str);

    if (str->len == 0)
        ErrResult;

    switch (*str->ptr)
    {
    case 'n':
    {
        return decode_json_null(str);
    }
    break;

    case 't':
    {
        return decode_json_true(str);
    }
    break;

    case 'f':
    {
        return decode_json_false(str);
    }
    break;

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '-':
    {
        return decode_json_number(str);
    }
    break;

    case '\"':
    {
        return decode_json_string(str);
    }
    break;

    case '[':
    {
        return decode_json_array(str);
    }
    break;

    case '{':
    {
        return decode_json_object(str);
    }
    break;
    }

    return ErrResult;
}

JsonResult decode_json(StringView str)
{
    JsonResult result = decode_json_value(&str);
    if (str.len != 0)
        return ErrResult;
    return result;
}

int main(void)
{
    DEFAULT_ARENA = arena_default_new();
    char input[] = "{\"hello\":1.2}";
    StringView view = sv_from_cstr(input, sizeof(input) - 1);
    JsonResult result = decode_json(view);
    if (result.error)
    {
        panic("cannot parse json");
    }
    encode_json(stdout, result.value);
    arena_free(DEFAULT_ARENA);
}