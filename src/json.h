#ifndef WPI_JSON_H
#define WPI_JSON_H

#include <wchar.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JNode {
    JType type;
    char *key;              /* имя поля, если узел лежит внутри объекта */
    char *str;              /* J_STR, UTF-8 */
    double num;             /* J_NUM; для J_BOOL — 0 или 1 */
    struct JNode *child;    /* первый элемент J_ARR / J_OBJ */
    struct JNode *next;     /* следующий элемент того же родителя */
} JNode;

/*
 * Разбирает JSON с послаблениями для ручного редактирования:
 * комментарии (// и блочные), висячие запятые, ключи без кавычек,
 * неизвестные escape-последовательности ("C:\Soft") остаются как есть.
 */
JNode *json_parse(const char *text, wchar_t *err, int errlen);
void json_free(JNode *n);

const JNode *json_get(const JNode *obj, const char *key);
const char *json_str(const JNode *obj, const char *key, const char *def);
double json_num(const JNode *obj, const char *key, double def);
int json_bool(const JNode *obj, const char *key, int def);

#endif
