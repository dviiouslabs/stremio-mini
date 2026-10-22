/* Minimal JSON DOM parser (no dependencies). */
#ifndef JSON_H
#define JSON_H

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jv {
    jtype t;
    double n;          /* number, or 0/1 for bool */
    char *s;           /* string value */
    struct jv **items; /* array items / object values */
    char **keys;       /* object keys */
    int len;
} jv;

jv *json_parse(const char *src);   /* NULL on error */
void json_free(jv *v);

jv *jget(const jv *obj, const char *key);   /* NULL if missing / not object */
jv *jat(const jv *arr, int i);              /* NULL if out of range */
int jlen(const jv *v);                      /* array/object length, else 0 */
const char *jstr(const jv *v, const char *def);
double jnum(const jv *v, double def);
int jtrue(const jv *v);

/* Returns malloc'd JSON string literal (with quotes) for s. */
char *json_quote(const char *s);

#endif
