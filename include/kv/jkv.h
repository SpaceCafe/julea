/*
 * JULEA - Flexible storage framework
 * Copyright (C) 2017 Michael Kuhn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 **/

#ifndef JULEA_KV_KV_H
#define JULEA_KV_KV_H

#if !defined(JULEA_KV_H) && !defined(JULEA_KV_COMPILATION)
#error "Only <julea-kv.h> can be included directly."
#endif

#include <glib.h>

struct JKV;

typedef struct JKV JKV;

#include <bson.h>

#include <julea.h>

typedef void (*JKVGetFunc) (bson_t const*, gpointer);

JKV* j_kv_new (gchar const*, gchar const*);
JKV* j_kv_new_for_index (guint32, gchar const*, gchar const*);
JKV* j_kv_ref (JKV*);
void j_kv_unref (JKV*);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(JKV, j_kv_unref)

void j_kv_put (JKV*, bson_t*, JBatch*);
void j_kv_delete (JKV*, JBatch*);

void j_kv_get (JKV*, bson_t*, JBatch*);
void j_kv_get_callback (JKV*, JKVGetFunc, gpointer, JBatch*);

#endif
