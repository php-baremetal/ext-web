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
 * The `http` source for the event-driven model: Baremetal\Http\Request / \Response and the global
 * serve_http() that starts an esp_http_server whose requests are delivered to the reactor as Request
 * events. The handler returns a Response, routed back to the same socket.
 */
#pragma once

#include "php.h"

extern zend_module_entry web_module_entry;
