/*
   +----------------------------------------------------------------------+
   | php-baremetal: ext-web                                               |
   +----------------------------------------------------------------------+
   | Copyright (c) 2026 Gianfrancesco Aurecchia                           |
   +----------------------------------------------------------------------+
   | This source file is subject to the MIT license, that is bundled with |
   | this package in the file LICENSE.                                    |
   +----------------------------------------------------------------------+
   | Authors: Gianfrancesco Aurecchia <gianfri@php-baremetal.com>         |
   +----------------------------------------------------------------------+
*/

/*
 * Baremetal\Http\Request/Response/Message + serve_http/serve_ws/ws_broadcast. The server task parks a
 * request and posts it to the reactor; the reactor (php_task) answers and the server task sends the reply.
 *
 * Neutral PHP binding: it talks only to the web HAL (bm_web_hal.h); the HTTP server lives in
 * bm_web_hal_<arch>.c.
 */
#include "php_web.h"
#include "bm_web_hal.h"
#include "event_bus_php.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "SAPI.h"             /* SG(), php_default_treat_data */
#include "php_variables.h"    /* PARSE_STRING */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static zend_class_entry *web_request_ce;
static zend_class_entry *web_response_ce;
static zend_class_entry *web_message_ce;   /* Baremetal\Http\Message -- an inbound WebSocket frame */

static bool s_started;

/* ---- Response construction (C-side) --------------------------------------------------------------- */

static void web_response_set(zval *out, zend_long status, zend_string *body, const char *ctype)
{
    object_init_ex(out, web_response_ce);
    zend_update_property_long(web_response_ce, Z_OBJ_P(out), "status", sizeof("status") - 1, status);
    zend_update_property_str(web_response_ce, Z_OBJ_P(out), "body", sizeof("body") - 1, body);
    zend_update_property_string(web_response_ce, Z_OBJ_P(out), "contentType",
                                sizeof("contentType") - 1, ctype);
}

static zend_string *web_json_encode(zval *data)
{
    zval fn, ret;
    ZVAL_STRING(&fn, "json_encode");
    zend_string *s = NULL;
    if (call_user_function(NULL, NULL, &fn, &ret, 1, data) == SUCCESS && Z_TYPE(ret) == IS_STRING) {
        s = zend_string_copy(Z_STR(ret));
    }
    zval_ptr_dtor(&fn);
    zval_ptr_dtor(&ret);
    return s ? s : ZSTR_EMPTY_ALLOC();
}

/* ---- Baremetal\Http\Response --------------------------------------------------------------------- */

ZEND_BEGIN_ARG_INFO_EX(arginfo_resp_ctor, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, status, IS_LONG, 0, "200")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, body, IS_STRING, 0, "\"\"")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, contentType, IS_STRING, 0, "\"text/html; charset=UTF-8\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_resp_body, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, body, IS_STRING, 0, "\"\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_resp_none, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_resp_json, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_resp_header, 0, 2, IS_STATIC, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, replace, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

PHP_METHOD(Response, __construct)
{
    zend_long status = 200;
    zend_string *body = NULL, *ctype = NULL;
    ZEND_PARSE_PARAMETERS_START(0, 3)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(status)
        Z_PARAM_STR(body)
        Z_PARAM_STR(ctype)
    ZEND_PARSE_PARAMETERS_END();

    zend_update_property_long(web_response_ce, Z_OBJ_P(ZEND_THIS), "status", sizeof("status") - 1, status);
    if (body) {
        zend_update_property_str(web_response_ce, Z_OBJ_P(ZEND_THIS), "body", sizeof("body") - 1, body);
    }
    if (ctype) {
        zend_update_property_str(web_response_ce, Z_OBJ_P(ZEND_THIS), "contentType",
                                 sizeof("contentType") - 1, ctype);
    }
}

PHP_METHOD(Response, ok)
{
    zend_string *body = NULL;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR(body)
    ZEND_PARSE_PARAMETERS_END();
    web_response_set(return_value, 200, body ? body : ZSTR_EMPTY_ALLOC(), "text/html; charset=UTF-8");
}

PHP_METHOD(Response, text)
{
    zend_string *body;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(body)
    ZEND_PARSE_PARAMETERS_END();
    web_response_set(return_value, 200, body, "text/plain; charset=UTF-8");
}

PHP_METHOD(Response, html)
{
    zend_string *body;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(body)
    ZEND_PARSE_PARAMETERS_END();
    web_response_set(return_value, 200, body, "text/html; charset=UTF-8");
}

PHP_METHOD(Response, json)
{
    zval *data;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(data)
    ZEND_PARSE_PARAMETERS_END();
    zend_string *body = web_json_encode(data);
    web_response_set(return_value, 200, body, "application/json");
    zend_string_release(body);
}

PHP_METHOD(Response, notFound)
{
    zend_string *body = NULL;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR(body)
    ZEND_PARSE_PARAMETERS_END();
    if (body) {
        web_response_set(return_value, 404, body, "text/plain; charset=UTF-8");
    } else {
        zend_string *d = zend_string_init("Not Found", 9, 0);
        web_response_set(return_value, 404, d, "text/plain; charset=UTF-8");
        zend_string_release(d);
    }
}

PHP_METHOD(Response, noContent)
{
    ZEND_PARSE_PARAMETERS_NONE();
    web_response_set(return_value, 204, ZSTR_EMPTY_ALLOC(), "text/plain; charset=UTF-8");
}

/* An RFC 9110 token. */
static bool web_header_name_ok(const char *p, size_t len)
{
    if (len == 0) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char) p[i];
        if (!isalnum(c) && !strchr("!#$%&'*+-.^_`|~", c)) {
            return false;
        }
    }
    return true;
}

static bool web_header_value_ok(const char *p, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (p[i] == '\r' || p[i] == '\n' || p[i] == '\0') {
            return false;
        }
    }
    return true;
}

/* Headers the server writes itself. */
static bool web_header_reserved(const char *p, size_t len)
{
    return (len == 14 && strncasecmp(p, "Content-Length", 14) == 0) ||
           (len == 17 && strncasecmp(p, "Transfer-Encoding", 17) == 0);
}

/* Response::header($name, $value, $replace = true): static -- adds a response header (Content-Type sets
 * $contentType); with $replace false a header of the same name is kept and this one is sent too. */
PHP_METHOD(Response, header)
{
    zend_string *name, *value;
    bool replace = true;
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STR(name)
        Z_PARAM_STR(value)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(replace)
    ZEND_PARSE_PARAMETERS_END();

    if (!web_header_name_ok(ZSTR_VAL(name), ZSTR_LEN(name))) {
        zend_argument_value_error(1, "must be a valid header name");
        RETURN_THROWS();
    }
    if (web_header_reserved(ZSTR_VAL(name), ZSTR_LEN(name))) {
        zend_argument_value_error(1, "is set by the server");
        RETURN_THROWS();
    }
    if (!web_header_value_ok(ZSTR_VAL(value), ZSTR_LEN(value))) {
        zend_argument_value_error(2, "must not contain CR, LF or NUL");
        RETURN_THROWS();
    }
    zend_object *self = Z_OBJ_P(ZEND_THIS);
    if (zend_string_equals_literal_ci(name, "Content-Type")) {
        zend_update_property_str(web_response_ce, self, "contentType", sizeof("contentType") - 1, value);
        RETURN_OBJ_COPY(self);
    }

    zval rv, headers;
    zval *cur = zend_read_property(web_response_ce, self, "headers", sizeof("headers") - 1, 1, &rv);
    if (cur && Z_TYPE_P(cur) == IS_ARRAY) {
        ZVAL_ARR(&headers, zend_array_dup(Z_ARRVAL_P(cur)));
    } else {
        array_init(&headers);
    }
    zend_string *key;
    zval *existing = NULL;
    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(headers), key, existing) {
        if (key && zend_string_equals_ci(key, name)) {
            break;
        }
        existing = NULL;
    } ZEND_HASH_FOREACH_END();
    if (existing && replace) {
        zend_hash_del(Z_ARRVAL(headers), key);
        existing = NULL;
    }
    if (!existing) {
        add_assoc_str_ex(&headers, ZSTR_VAL(name), ZSTR_LEN(name), zend_string_copy(value));
    } else {
        if (Z_ISREF_P(existing)) {
            /* the copy must not write through a reference into the old array */
            zval plain;
            ZVAL_COPY(&plain, Z_REFVAL_P(existing));
            zval_ptr_dtor(existing);
            ZVAL_COPY_VALUE(existing, &plain);
        }
        if (Z_TYPE_P(existing) != IS_ARRAY) {
            zval old;
            ZVAL_COPY_VALUE(&old, existing);
            array_init(existing);
            add_next_index_zval(existing, &old);   /* takes the old value's reference */
        } else {
            SEPARATE_ARRAY(existing);   /* the list is shared with the old $headers */
        }
        add_next_index_str(existing, zend_string_copy(value));
    }
    zend_update_property(web_response_ce, self, "headers", sizeof("headers") - 1, &headers);
    zval_ptr_dtor(&headers);
    RETURN_OBJ_COPY(self);
}

static const zend_function_entry response_methods[] = {
    PHP_ME(Response, __construct, arginfo_resp_ctor, ZEND_ACC_PUBLIC)
    PHP_ME(Response, ok,        arginfo_resp_body, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Response, text,      arginfo_resp_body, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Response, html,      arginfo_resp_body, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Response, json,      arginfo_resp_json, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Response, notFound,  arginfo_resp_body, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Response, noContent, arginfo_resp_none, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Response, header,    arginfo_resp_header, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ---- Baremetal\Http\Request ---------------------------------------------------------------------- */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_req_json, 0, 0, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_req_header, 0, 1, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* Request::json(): json_decode($this->body, true) -- the request body as an associative value. */
PHP_METHOD(Request, json)
{
    ZEND_PARSE_PARAMETERS_NONE();
    zval rv;
    zval *body = zend_read_property(web_request_ce, Z_OBJ_P(ZEND_THIS), "body", sizeof("body") - 1, 0, &rv);
    if (!body || Z_TYPE_P(body) != IS_STRING) {
        RETURN_NULL();
    }
    zval fn, args[2];
    ZVAL_STRING(&fn, "json_decode");
    ZVAL_STR_COPY(&args[0], Z_STR_P(body));
    ZVAL_TRUE(&args[1]);
    call_user_function(NULL, NULL, &fn, return_value, 2, args);
    zval_ptr_dtor(&fn);
    zval_ptr_dtor(&args[0]);
}

/* Request::header($name): a request header by (case-insensitive) name, or null. */
PHP_METHOD(Request, header)
{
    zend_string *name;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(name)
    ZEND_PARSE_PARAMETERS_END();

    zval rv;
    zval *hdrs = zend_read_property(web_request_ce, Z_OBJ_P(ZEND_THIS), "headers",
                                    sizeof("headers") - 1, 0, &rv);
    if (!hdrs || Z_TYPE_P(hdrs) != IS_ARRAY) {
        RETURN_NULL();
    }
    zend_string *key;
    zval *val;
    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(hdrs), key, val) {
        if (key && zend_string_equals_ci(key, name)) {
            RETURN_COPY(val);
        }
    } ZEND_HASH_FOREACH_END();
    RETURN_NULL();
}

static const zend_function_entry request_methods[] = {
    PHP_ME(Request, json,   arginfo_req_json,   ZEND_ACC_PUBLIC)
    PHP_ME(Request, header, arginfo_req_header, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ---- The request/response rendezvous (reactor side) ---------------------------------------------- */

/* Appends one response header to the slot ("name\0value\0"); invalid or excess ones are dropped with a
 * warning (the $headers property can be written directly, bypassing header()). */
static void web_add_header(web_slot_t *s, zend_string *name, zval *v, size_t *cap)
{
    zend_string *value = zval_get_string(v);
    if (!web_header_name_ok(ZSTR_VAL(name), ZSTR_LEN(name)) || web_header_reserved(ZSTR_VAL(name), ZSTR_LEN(name)) ||
        !web_header_value_ok(ZSTR_VAL(value), ZSTR_LEN(value))) {
        php_error_docref(NULL, E_WARNING, "Response header \"%s\" dropped (invalid or set by the server)", ZSTR_VAL(name));
    } else if (s->n_resp_hdr >= WEB_MAX_RESP_HDRS) {
        php_error_docref(NULL, E_WARNING, "Response header \"%s\" dropped (more than %d headers)", ZSTR_VAL(name),
                         WEB_MAX_RESP_HDRS);
    } else {
        size_t need = s->resp_hdrs_len + ZSTR_LEN(name) + ZSTR_LEN(value) + 2;
        if (need > *cap) {
            size_t size = need * 2;
            char *p = realloc(s->resp_hdrs, size);
            if (!p) {
                zend_string_release(value);
                return;
            }
            s->resp_hdrs = p;
            *cap = size;
        }
        char *dst = s->resp_hdrs + s->resp_hdrs_len;
        memcpy(dst, ZSTR_VAL(name), ZSTR_LEN(name) + 1);
        memcpy(dst + ZSTR_LEN(name) + 1, ZSTR_VAL(value), ZSTR_LEN(value) + 1);
        s->resp_hdrs_len = need;
        s->n_resp_hdr++;
    }
    zend_string_release(value);
}

/* The response Content-Type; one with CR, LF or NUL is dropped with a warning. */
static void web_set_ctype(web_slot_t *s, zend_string *ct)
{
    if (!web_header_value_ok(ZSTR_VAL(ct), ZSTR_LEN(ct))) {
        php_error_docref(NULL, E_WARNING, "Response Content-Type dropped (contains CR, LF or NUL)");
        return;
    }
    snprintf(s->resp_ctype, sizeof s->resp_ctype, "%s", ZSTR_VAL(ct));
}

static void web_take_headers(web_slot_t *s, zend_object *resp)
{
    zval rv;
    zval *prop = zend_read_property(web_response_ce, resp, "headers", sizeof("headers") - 1, 1, &rv);
    if (!prop) {
        return;
    }
    ZVAL_DEREF(prop);
    if (Z_TYPE_P(prop) != IS_ARRAY) {
        return;
    }
    /* A warning can run a user error handler that reassigns $headers: hold our own reference. */
    zval hs;
    ZVAL_COPY(&hs, prop);
    size_t cap = 0;
    zend_string *name;
    zval *v;
    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(hs), name, v) {
        if (!name) {
            continue;
        }
        ZVAL_DEREF(v);
        if (zend_string_equals_literal_ci(name, "Content-Type")) {
            zend_string *ct = zval_get_string(v);
            web_set_ctype(s, ct);
            zend_string_release(ct);
        } else if (Z_TYPE_P(v) == IS_ARRAY) {
            zval list, *item;
            ZVAL_COPY(&list, v);
            ZEND_HASH_FOREACH_VAL(Z_ARRVAL(list), item) {
                ZVAL_DEREF(item);
                web_add_header(s, name, item, &cap);
            } ZEND_HASH_FOREACH_END();
            zval_ptr_dtor(&list);
        } else {
            web_add_header(s, name, v, &cap);
        }
    } ZEND_HASH_FOREACH_END();
    zval_ptr_dtor(&hs);
}

/* Copy Response object fields into the slot for the server task to send. */
static void web_take_response(web_slot_t *s, zend_object *resp)
{
    zval rv;
    zval *st = zend_read_property(web_response_ce, resp, "status", sizeof("status") - 1, 1, &rv);
    zend_long status = st ? zval_get_long(st) : 200;
    s->status = status < 100 || status > 599 ? 500 : (int) status;

    zval *ct = zend_read_property(web_response_ce, resp, "contentType", sizeof("contentType") - 1, 1, &rv);
    snprintf(s->resp_ctype, sizeof s->resp_ctype, "text/html; charset=UTF-8");
    if (ct) {
        ZVAL_DEREF(ct);
    }
    if (ct && Z_TYPE_P(ct) == IS_STRING) {
        web_set_ctype(s, Z_STR_P(ct));
    }

    web_take_headers(s, resp);

    zval *bd = zend_read_property(web_response_ce, resp, "body", sizeof("body") - 1, 1, &rv);
    if (bd && Z_TYPE_P(bd) == IS_STRING && Z_STRLEN_P(bd) > 0) {
        s->resp_len = Z_STRLEN_P(bd);
        s->resp_body = malloc(s->resp_len);
        if (s->resp_body) {
            memcpy(s->resp_body, Z_STRVAL_P(bd), s->resp_len);
        } else {
            s->resp_len = 0;
        }
    }
}

static void web_set_plain(web_slot_t *s, int status, const char *body)
{
    s->status = status;
    snprintf(s->resp_ctype, sizeof s->resp_ctype, "text/plain; charset=UTF-8");
    size_t n = strlen(body);
    s->resp_body = malloc(n);
    if (s->resp_body) {
        memcpy(s->resp_body, body, n);
        s->resp_len = n;
    }
}

/* Parse a "a=1&b=2&x[]=..." string into dst (same rules as parse_str / $_GET). */
static void web_parse_qs(zval *dst, const char *s, size_t len)
{
    array_init(dst);
    if (!s || len == 0) {
        return;
    }
    char *dup = estrndup(s, len);
    php_default_treat_data(PARSE_STRING, dup, dst);   /* takes and frees dup */
}

/* Set a superglobal, activating the auto-global first so the engine won't rebuild it on access. */
static void web_put_global(const char *name, size_t nlen, zval *arr)
{
    zend_is_auto_global_str((char *) name, nlen);
    zend_hash_str_update(&EG(symbol_table), name, nlen, arr);
}

/* Populate $_GET/$_POST/$_REQUEST/$_SERVER for this request (the engine is resident, no SAPI cycle). */
static void web_populate_superglobals(web_slot_t *s)
{
    zval get, post, request, server;

    web_parse_qs(&get, s->query, strlen(s->query));

    bool is_form = false;
    for (int i = 0; i < s->n_hdr; i++) {
        if (strcasecmp(s->hdr[i].name, "Content-Type") == 0 &&
            strstr(s->hdr[i].value, "application/x-www-form-urlencoded")) {
            is_form = true;
        }
    }
    if (is_form && s->body && s->body_len) {
        web_parse_qs(&post, s->body, s->body_len);
    } else {
        array_init(&post);
    }

    array_init(&request);                              /* GET then POST, POST wins (request_order GP) */
    zend_hash_merge(Z_ARRVAL(request), Z_ARRVAL(get),  zval_add_ref, 1);
    zend_hash_merge(Z_ARRVAL(request), Z_ARRVAL(post), zval_add_ref, 1);

    zend_is_auto_global_str(ZEND_STRL("_SERVER"));       /* the startup $_SERVER (SCRIPT_FILENAME, env, ...) */
    zval *base = &PG(http_globals)[TRACK_VARS_SERVER];
    if (Z_TYPE_P(base) == IS_ARRAY) {
        ZVAL_ARR(&server, zend_array_dup(Z_ARRVAL_P(base)));
    } else {
        array_init(&server);
    }
    add_assoc_string(&server, "REQUEST_METHOD", (char *) s->method);
    char uri[512];
    if (s->query[0]) {
        snprintf(uri, sizeof uri, "%s?%s", s->path, s->query);
    } else {
        snprintf(uri, sizeof uri, "%s", s->path);
    }
    add_assoc_string(&server, "REQUEST_URI", uri);
    add_assoc_string(&server, "QUERY_STRING", s->query);
    add_assoc_string(&server, "REMOTE_ADDR", s->ip);
    add_assoc_string(&server, "SERVER_SOFTWARE", "phpbm");
    add_assoc_string(&server, "SERVER_PROTOCOL", "HTTP/1.1");
    if (s->body_len) {
        add_assoc_long(&server, "CONTENT_LENGTH", (zend_long) s->body_len);
    }
    for (int i = 0; i < s->n_hdr; i++) {
        if (strcasecmp(s->hdr[i].name, "Content-Type") == 0) {
            add_assoc_string(&server, "CONTENT_TYPE", s->hdr[i].value);
            continue;
        }
        char key[80] = "HTTP_";                        /* Header-Name -> HTTP_HEADER_NAME */
        size_t k = 5;
        for (const char *p = s->hdr[i].name; *p && k < sizeof key - 1; p++) {
            char c = *p;
            key[k++] = (c == '-') ? '_' : (char) toupper((unsigned char) c);
        }
        key[k] = '\0';
        add_assoc_string(&server, key, s->hdr[i].value);
    }

    web_put_global("_GET", sizeof("_GET") - 1, &get);
    web_put_global("_POST", sizeof("_POST") - 1, &post);
    web_put_global("_REQUEST", sizeof("_REQUEST") - 1, &request);
    web_put_global("_SERVER", sizeof("_SERVER") - 1, &server);
}

/* max_execution_time for the HTTP handlers: request-timeout while the Request is delivered, then the
 * script's own value again (old holds it). Through the ini entry, so ini_get() reports it and the
 * handler's set_time_limit() lasts only for that request. */
static void web_set_time_limit(const char *value)
{
    zend_string *name = zend_string_init(ZEND_STRL("max_execution_time"), 0);
    zend_alter_ini_entry_chars(name, value, strlen(value), PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
    zend_string_release(name);
}

/* Sets request-timeout and returns the script's value (the caller releases it). */
static zend_string *web_time_limit_begin(void)
{
    const char *current = zend_ini_string_ex("max_execution_time", sizeof("max_execution_time") - 1, 0, NULL);
    zend_string *old = zend_string_init(current != NULL ? current : "0", current != NULL ? strlen(current) : 1, 0);
    char value[16];
    snprintf(value, sizeof value, "%u", (unsigned) bm_web_hal_request_timeout());
    web_set_time_limit(value);
    return old;
}

/* Runs on the PHP task (inside phpbm_invoke): build the Request, dispatch, take the Response, release the
 * server task (via the HAL). A handler's exception yields 500 and is left pending, so it follows on-error;
 * a fatal yields 500 too and then bails out. Registered with bm_events_set_http_handler. */
static void web_deliver(void *handle)
{
    web_slot_t *s = handle;
    if (bm_web_hal_slot_abandoned(s)) {
        bm_web_hal_response_ready(s);   /* the client already got 503: frees the slot */
        return;
    }

    zval req;
    object_init_ex(&req, web_request_ce);
    zend_update_property_string(web_request_ce, Z_OBJ(req), "method", sizeof("method") - 1, s->method);
    zend_update_property_string(web_request_ce, Z_OBJ(req), "path", sizeof("path") - 1, s->path);
    zend_update_property_string(web_request_ce, Z_OBJ(req), "query", sizeof("query") - 1, s->query);
    zend_update_property_string(web_request_ce, Z_OBJ(req), "ip", sizeof("ip") - 1, s->ip);
    zend_update_property_stringl(web_request_ce, Z_OBJ(req), "body", sizeof("body") - 1,
                                 s->body ? s->body : "", s->body_len);
    zval hdrs;
    array_init(&hdrs);
    for (int i = 0; i < s->n_hdr; i++) {
        add_assoc_string(&hdrs, s->hdr[i].name, s->hdr[i].value);
    }
    zend_update_property(web_request_ce, Z_OBJ(req), "headers", sizeof("headers") - 1, &hdrs);
    zval_ptr_dtor(&hdrs);

    s->status = 0;
    s->resp_body = NULL;
    s->resp_len = 0;
    s->resp_ctype[0] = '\0';

    bool bailed = false;
    zend_string *time_limit = web_time_limit_begin();
    zend_try {
        web_populate_superglobals(s);
        zval resp;
        int rc = bm_events_deliver_answering(&req, &resp);
        if (rc == 1) {
            if (Z_TYPE(resp) == IS_OBJECT && instanceof_function(Z_OBJCE(resp), web_response_ce)) {
                web_take_response(s, Z_OBJ(resp));
            } else {
                web_set_plain(s, 500, "handler did not return a Response");
            }
            zval_ptr_dtor(&resp);
        } else if (rc == -1) {
            web_set_plain(s, 500, "Internal Server Error");
        } else if (rc == -2) {
            web_set_plain(s, 503, "Service Unavailable");   /* no Request listener */
        } else {
            web_set_plain(s, 404, "Not Found");
        }
    } zend_catch {
        bailed = true;
    } zend_end_try();
    if (!bailed) {
        web_set_time_limit(ZSTR_VAL(time_limit));   /* a fatal or exit() stops the program: nothing to restore */
    }
    zend_string_release(time_limit);

    if (s->status == 0) {
        web_set_plain(s, 500, "Internal Server Error");
    }
    bm_web_hal_response_ready(s);   /* s may be freed from here on */
    if (bailed) {
        zend_bailout();
    }
    zval_ptr_dtor(&req);
}

/* ---- the WebSocket source (reactor side) --------------------------------------------------------- */

/* Runs on the PHP task (inside phpbm_invoke): build a Message and deliver it to the listeners. A handler's
 * exception is left pending, so it follows on-error. */
static void web_ws_deliver(void *handle)
{
    ws_msg_t *m = handle;
    zval ev;
    object_init_ex(&ev, web_message_ce);
    zend_update_property_stringl(web_message_ce, Z_OBJ(ev), "text", sizeof("text") - 1, m->data, m->len);
    zend_update_property_long(web_message_ce, Z_OBJ(ev), "client", sizeof("client") - 1, m->client);
    zend_update_property_bool(web_message_ce, Z_OBJ(ev), "binary", sizeof("binary") - 1, m->binary);
    free(m);
    bm_events_deliver_object(&ev);
    zval_ptr_dtor(&ev);
}

/* ---- serve_http() / serve_ws() ------------------------------------------------------------------- */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_serve_http, 0, 0, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, port, IS_LONG, 0, "80")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_serve_ws, 0, 0, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, path, IS_STRING, 0, "\"/ws\"")
ZEND_END_ARG_INFO()

/* serve_http(int $port = 80): bool -- start the HTTP server; requests arrive as Request events. */
PHP_FUNCTION(serve_http)
{
    zend_long port = 80;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(port)
    ZEND_PARSE_PARAMETERS_END();

    if (port < 1 || port > 65535) {
        zend_argument_value_error(1, "must be between 1 and 65535");
        RETURN_THROWS();
    }
    if (bm_web_hal_port() != 0 && bm_web_hal_port() != port) {
        zend_throw_exception_ex(zend_ce_exception, 0, "serve_http(): the server already runs on port %u", (unsigned) bm_web_hal_port());
        RETURN_THROWS();
    }
    if (s_started) {
        RETURN_TRUE;
    }
    if (!bm_web_hal_network_ready()) {
        zend_throw_exception(zend_ce_exception,
            "serve_http(): no network interface (turn the network option on, or connect with ext-wifi first)", 0);
        RETURN_THROWS();
    }
    /* Handlers run with max_execution_time = request-timeout and the server answers 503 at it + 5 s;
     * 0: no deadline (a handler that never returns blocks the server). */
    uint32_t timeout = bm_web_hal_request_timeout();
    bm_web_hal_set_limits(timeout > 0 ? timeout + 5 : 0,
                          SG(post_max_size) > 0 ? (size_t) SG(post_max_size) : 0);
    if (!bm_web_hal_start((uint16_t) port)) {
        zend_throw_exception(zend_ce_exception, "serve_http(): the HTTP server failed to start", 0);
        RETURN_THROWS();
    }
    bm_events_set_http_handler(web_deliver);
    if (!bm_web_hal_register_http()) {
        zend_throw_exception(zend_ce_exception, "serve_http(): cannot register the HTTP routes", 0);
        RETURN_THROWS();
    }
    s_started = true;
    RETURN_TRUE;
}

/* serve_ws(string $path = "/ws"): bool -- WebSocket endpoint; inbound frames arrive as Message events. */
PHP_FUNCTION(serve_ws)
{
    zend_string *path = NULL;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR(path)
    ZEND_PARSE_PARAMETERS_END();
    const char *p = path ? ZSTR_VAL(path) : "/ws";

    if (!bm_web_hal_network_ready()) {
        zend_throw_exception(zend_ce_exception,
            "serve_ws(): no network interface (turn the network option on, or connect with ext-wifi first)", 0);
        RETURN_THROWS();
    }
    if (!bm_web_hal_start(bm_web_hal_port() != 0 ? bm_web_hal_port() : 80)) {
        zend_throw_exception(zend_ce_exception, "serve_ws(): the HTTP server failed to start", 0);
        RETURN_THROWS();
    }
    bm_events_set_ws_handler(web_ws_deliver);
    if (!bm_web_hal_register_ws(p)) {
        zend_throw_exception(zend_ce_exception, "serve_ws(): cannot register the WS handler", 0);
        RETURN_THROWS();
    }
    RETURN_TRUE;
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ws_reply, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* Message::reply(string $data): bool -- send a text frame back to the client this message came from. */
PHP_METHOD(Message, reply)
{
    zend_string *data;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(data)
    ZEND_PARSE_PARAMETERS_END();

    zval rv;
    zval *client = zend_read_property(web_message_ce, Z_OBJ_P(ZEND_THIS), "client", sizeof("client") - 1, 0, &rv);
    if (!client) {
        RETURN_FALSE;
    }
    zend_long id = zval_get_long(client);
    if (id <= 0 || (zend_ulong) id > (zend_ulong) INT32_MAX) {
        RETURN_FALSE;
    }
    RETURN_BOOL(bm_web_hal_ws_send((int32_t) id, ZSTR_VAL(data), ZSTR_LEN(data)));
}

static const zend_function_entry message_methods[] = {
    PHP_ME(Message, reply, arginfo_ws_reply, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ws_broadcast, 0, 1, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* ws_broadcast(string $data): int -- send a text frame to every WS client, returns how many reached. */
PHP_FUNCTION(ws_broadcast)
{
    zend_string *data;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(data)
    ZEND_PARSE_PARAMETERS_END();
    RETURN_LONG(bm_web_hal_ws_broadcast(ZSTR_VAL(data), ZSTR_LEN(data)));
}

/* ---- module ------------------------------------------------------------------------------------- */

PHP_MINIT_FUNCTION(web)
{
    zend_class_entry ce;
    zend_class_entry *event_base = bm_events_base_ce();   /* a Request is a request/response event */

    INIT_NS_CLASS_ENTRY(ce, "Baremetal\\Http", "Request", request_methods);
    web_request_ce = event_base ? zend_register_internal_class_ex(&ce, event_base)
                                : zend_register_internal_class(&ce);
    zend_declare_property_string(web_request_ce, "method", sizeof("method") - 1, "GET", ZEND_ACC_PUBLIC);
    zend_declare_property_string(web_request_ce, "path", sizeof("path") - 1, "/", ZEND_ACC_PUBLIC);
    zend_declare_property_string(web_request_ce, "query", sizeof("query") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(web_request_ce, "body", sizeof("body") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(web_request_ce, "ip", sizeof("ip") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_null(web_request_ce, "headers", sizeof("headers") - 1, ZEND_ACC_PUBLIC);

    INIT_NS_CLASS_ENTRY(ce, "Baremetal\\Http", "Response", response_methods);
    web_response_ce = zend_register_internal_class(&ce);
    zend_declare_property_long(web_response_ce, "status", sizeof("status") - 1, 200, ZEND_ACC_PUBLIC);
    zend_declare_property_string(web_response_ce, "body", sizeof("body") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(web_response_ce, "contentType", sizeof("contentType") - 1,
                                 "text/html; charset=UTF-8", ZEND_ACC_PUBLIC);
    zval no_headers;
    ZVAL_EMPTY_ARRAY(&no_headers);
    zend_declare_property(web_response_ce, "headers", sizeof("headers") - 1, &no_headers, ZEND_ACC_PUBLIC);

    /* Baremetal\Http\Message -- an inbound WebSocket frame (a fire-and-forget event). */
    INIT_NS_CLASS_ENTRY(ce, "Baremetal\\Http", "Message", message_methods);
    web_message_ce = event_base ? zend_register_internal_class_ex(&ce, event_base)
                                : zend_register_internal_class(&ce);
    zend_declare_property_string(web_message_ce, "text", sizeof("text") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_long(web_message_ce, "client", sizeof("client") - 1, 0, ZEND_ACC_PUBLIC);
    zend_declare_property_bool(web_message_ce, "binary", sizeof("binary") - 1, 0, ZEND_ACC_PUBLIC);
    return SUCCESS;
}

static const zend_function_entry web_functions[] = {
    PHP_FE(serve_http,   arginfo_serve_http)
    PHP_FE(serve_ws,     arginfo_serve_ws)
    PHP_FE(ws_broadcast, arginfo_ws_broadcast)
    PHP_FE_END
};

zend_module_entry web_module_entry = {
    STANDARD_MODULE_HEADER,
    "web",
    web_functions,
    PHP_MINIT(web),
    NULL,   /* MSHUTDOWN */
    NULL,   /* RINIT */
    NULL,   /* RSHUTDOWN */
    NULL,   /* MINFO */
    "1.0",
    STANDARD_MODULE_PROPERTIES
};
