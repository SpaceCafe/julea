/*
 * JULEA - Flexible storage framework
 * Copyright (C) 2010-2017 Michael Kuhn
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

#ifndef JULEA_ITEM_ITEM_ITERATOR_H
#define JULEA_ITEM_ITEM_ITERATOR_H

#if !defined(JULEA_ITEM_H) && !defined(JULEA_ITEM_COMPILATION)
#error "Only <julea-item.h> can be included directly."
#endif

#include <glib.h>

struct JItemIterator;

typedef struct JItemIterator JItemIterator;

#include <item/jcollection.h>
#include <item/jitem.h>

JItemIterator* j_item_iterator_new (JCollection*);
void j_item_iterator_free (JItemIterator*);

gboolean j_item_iterator_next (JItemIterator*);
JItem* j_item_iterator_get (JItemIterator*);

#endif
