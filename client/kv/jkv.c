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

#include <julea-config.h>

#include <glib.h>

#include <string.h>

#include <bson.h>

#include <kv/jkv.h>

#include <julea.h>

/**
 * \defgroup JKV KV
 *
 * Data structures and functions for managing objects.
 *
 * @{
 **/

struct JKVOperation
{
	union
	{
		struct
		{
			JKV* kv;
			bson_t* value;
			JKVGetFunc func;
			gpointer data;
		}
		get;

		struct
		{
			JKV* kv;
			bson_t* value;
		}
		put;
	};
};

typedef struct JKVOperation JKVOperation;

/**
 * A JKV.
 **/
struct JKV
{
	/**
	 * The data server index.
	 */
	guint32 index;

	/**
	 * The namespace.
	 **/
	gchar* namespace;

	/**
	 * The key.
	 **/
	gchar* key;

	/**
	 * The reference count.
	 **/
	gint ref_count;
};

static
void
j_kv_put_free (gpointer data)
{
	JKVOperation* operation = data;

	j_kv_unref(operation->put.kv);
	bson_destroy(operation->put.value);
	// FIXME
	g_slice_free(bson_t, operation->put.value);

	g_slice_free(JKVOperation, operation);
}

static
void
j_kv_delete_free (gpointer data)
{
	JKV* kv = data;

	j_kv_unref(kv);
}

static
void
j_kv_get_free (gpointer data)
{
	JKVOperation* operation = data;

	j_kv_unref(operation->get.kv);

	g_slice_free(JKVOperation, operation);
}

static
gboolean
j_kv_put_exec (JList* operations, JSemantics* semantics)
{
	gboolean ret = TRUE;

	JBackend* kv_backend;
	g_autoptr(JListIterator) it = NULL;
	g_autoptr(JMessage) message = NULL;
	JSemanticsSafety safety;
	gchar const* namespace;
	gpointer kv_batch;
	gsize namespace_len;
	guint32 index;

	g_return_val_if_fail(operations != NULL, FALSE);
	g_return_val_if_fail(semantics != NULL, FALSE);

	j_trace_enter(G_STRFUNC, NULL);

	{
		JKVOperation* kop;

		kop = j_list_get_first(operations);
		g_assert(kop != NULL);

		namespace = kop->put.kv->namespace;
		namespace_len = strlen(namespace) + 1;
		index = kop->put.kv->index;
	}

	safety = j_semantics_get(semantics, J_SEMANTICS_SAFETY);
	it = j_list_iterator_new(operations);
	kv_backend = j_kv_backend();

	if (kv_backend != NULL)
	{
		ret = j_backend_kv_batch_start(kv_backend, namespace, safety, &kv_batch);
	}
	else
	{
		/**
		 * Force safe semantics to make the server send a reply.
		 * Otherwise, nasty races can occur when using unsafe semantics:
		 * - The client creates the item and sends its first write.
		 * - The client sends another operation using another connection from the pool.
		 * - The second operation is executed first and fails because the item does not exist.
		 * This does not completely eliminate all races but fixes the common case of create, write, write, ...
		 **/
		message = j_message_new(J_MESSAGE_KV_PUT, namespace_len);
		j_message_set_safety(message, semantics);
		//j_message_force_safety(message, J_SEMANTICS_SAFETY_NETWORK);
		j_message_append_n(message, namespace, namespace_len);
	}

	while (j_list_iterator_next(it))
	{
		JKVOperation* kop = j_list_iterator_get(it);

		if (kv_backend != NULL)
		{
			ret = j_backend_kv_put(kv_backend, kv_batch, kop->put.kv->key, kop->put.value) && ret;
		}
		else
		{
			gsize key_len;

			key_len = strlen(kop->put.kv->key) + 1;

			j_message_add_operation(message, key_len + 4 + kop->put.value->len);
			j_message_append_n(message, kop->put.kv->key, key_len);
			j_message_append_4(message, &(kop->put.value->len));
			j_message_append_n(message, bson_get_data(kop->put.value), kop->put.value->len);
		}
	}

	if (kv_backend != NULL)
	{
		ret = j_backend_kv_batch_execute(kv_backend, kv_batch) && ret;
	}
	else
	{
		GSocketConnection* kv_connection;

		kv_connection = j_connection_pool_pop_kv(index);
		j_message_send(message, kv_connection);

		if (j_message_get_flags(message) & J_MESSAGE_FLAGS_SAFETY_NETWORK)
		{
			g_autoptr(JMessage) reply = NULL;

			reply = j_message_new_reply(message);
			j_message_receive(reply, kv_connection);

			/* FIXME do something with reply */
		}

		j_connection_pool_push_kv(index, kv_connection);
	}

	j_trace_leave(G_STRFUNC);

	return ret;
}

static
gboolean
j_kv_delete_exec (JList* operations, JSemantics* semantics)
{
	gboolean ret = TRUE;

	JBackend* kv_backend;
	g_autoptr(JListIterator) it = NULL;
	g_autoptr(JMessage) message = NULL;
	JSemanticsSafety safety;
	gchar const* namespace;
	gpointer kv_batch;
	gsize namespace_len;
	guint32 index;

	g_return_val_if_fail(operations != NULL, FALSE);
	g_return_val_if_fail(semantics != NULL, FALSE);

	j_trace_enter(G_STRFUNC, NULL);

	{
		JKV* object;

		object = j_list_get_first(operations);
		g_assert(object != NULL);

		namespace = object->namespace;
		namespace_len = strlen(namespace) + 1;
		index = object->index;
	}

	safety = j_semantics_get(semantics, J_SEMANTICS_SAFETY);
	it = j_list_iterator_new(operations);
	kv_backend = j_kv_backend();

	if (kv_backend != NULL)
	{
		ret = j_backend_kv_batch_start(kv_backend, namespace, safety, &kv_batch);
	}
	else
	{
		message = j_message_new(J_MESSAGE_KV_DELETE, namespace_len);
		j_message_set_safety(message, semantics);
		j_message_append_n(message, namespace, namespace_len);
	}

	while (j_list_iterator_next(it))
	{
		JKV* kv = j_list_iterator_get(it);

		if (kv_backend != NULL)
		{
			ret = j_backend_kv_delete(kv_backend, kv_batch, kv->key) && ret;
		}
		else
		{
			gsize key_len;

			key_len = strlen(kv->key) + 1;

			j_message_add_operation(message, key_len);
			j_message_append_n(message, kv->key, key_len);
		}
	}

	if (kv_backend != NULL)
	{
		ret = j_backend_kv_batch_execute(kv_backend, kv_batch) && ret;
	}
	else
	{
		GSocketConnection* kv_connection;

		kv_connection = j_connection_pool_pop_kv(index);
		j_message_send(message, kv_connection);

		if (j_message_get_flags(message) & J_MESSAGE_FLAGS_SAFETY_NETWORK)
		{
			g_autoptr(JMessage) reply = NULL;

			reply = j_message_new_reply(message);
			j_message_receive(reply, kv_connection);

			/* FIXME do something with reply */
		}

		j_connection_pool_push_kv(index, kv_connection);
	}

	j_trace_leave(G_STRFUNC);

	return ret;
}

static
gboolean
j_kv_get_exec (JList* operations, JSemantics* semantics)
{
	gboolean ret = TRUE;

	JBackend* kv_backend;
	g_autoptr(JListIterator) it = NULL;
	g_autoptr(JMessage) message = NULL;
	//JSemanticsSafety safety;
	gchar const* namespace;
	gsize namespace_len;
	guint32 index;

	g_return_val_if_fail(operations != NULL, FALSE);
	g_return_val_if_fail(semantics != NULL, FALSE);

	j_trace_enter(G_STRFUNC, NULL);

	{
		JKVOperation* kop;

		kop = j_list_get_first(operations);
		g_assert(kop != NULL);

		namespace = kop->get.kv->namespace;
		namespace_len = strlen(namespace) + 1;
		index = kop->get.kv->index;
	}

	//safety = j_semantics_get(semantics, J_SEMANTICS_SAFETY);
	it = j_list_iterator_new(operations);
	kv_backend = j_kv_backend();

	if (kv_backend == NULL)
	{
		/**
		 * Force safe semantics to make the server send a reply.
		 * Otherwise, nasty races can occur when using unsafe semantics:
		 * - The client creates the item and sends its first write.
		 * - The client sends another operation using another connection from the pool.
		 * - The second operation is executed first and fails because the item does not exist.
		 * This does not completely eliminate all races but fixes the common case of create, write, write, ...
		 **/
		message = j_message_new(J_MESSAGE_KV_GET, namespace_len);
		j_message_set_safety(message, semantics);
		//j_message_force_safety(message, J_SEMANTICS_SAFETY_NETWORK);
		j_message_append_n(message, namespace, namespace_len);
	}

	while (j_list_iterator_next(it))
	{
		JKVOperation* kop = j_list_iterator_get(it);

		if (kv_backend != NULL)
		{
			if (kop->get.func != NULL)
			{
				bson_t tmp[1];

				ret = j_backend_kv_get(kv_backend, kop->get.kv->namespace, kop->get.kv->key, tmp) && ret;

				if (ret)
				{
					kop->get.func(tmp, kop->get.data);
					bson_destroy(tmp);
				}
			}
			else
			{
				ret = j_backend_kv_get(kv_backend, kop->get.kv->namespace, kop->get.kv->key, kop->get.value) && ret;
			}
		}
		else
		{
			gsize key_len;

			key_len = strlen(kop->get.kv->key) + 1;

			j_message_add_operation(message, key_len);
			j_message_append_n(message, kop->get.kv->key, key_len);
		}
	}

	if (kv_backend == NULL)
	{
		g_autoptr(JListIterator) iter = NULL;
		g_autoptr(JMessage) reply = NULL;
		GSocketConnection* kv_connection;

		kv_connection = j_connection_pool_pop_kv(index);
		j_message_send(message, kv_connection);

		reply = j_message_new_reply(message);
		j_message_receive(reply, kv_connection);

		iter = j_list_iterator_new(operations);

		while (j_list_iterator_next(iter))
		{
			JKVOperation* kop = j_list_iterator_get(iter);
			guint32 len;

			len = j_message_get_4(reply);
			ret = (len > 0) && ret;

			if (len > 0)
			{
				bson_t tmp[1];
				gconstpointer data;

				data = j_message_get_n(reply, len);

				// FIXME check whether copies can be avoided
				bson_init_static(tmp, data, len);

				if (kop->get.func != NULL)
				{
					kop->get.func(tmp, kop->get.data);
				}
				else
				{
					bson_copy_to(tmp, kop->get.value);
				}
			}
		}

		j_connection_pool_push_kv(index, kv_connection);
	}

	j_trace_leave(G_STRFUNC);

	return ret;
}

/**
 * Creates a new item.
 *
 * \author Michael Kuhn
 *
 * \code
 * JKV* i;
 *
 * i = j_kv_new("JULEA");
 * \endcode
 *
 * \param key         An item key.
 * \param distribution A distribution.
 *
 * \return A new item. Should be freed with j_kv_unref().
 **/
JKV*
j_kv_new (gchar const* namespace, gchar const* key)
{
	JConfiguration* configuration = j_configuration();
	JKV* kv;

	g_return_val_if_fail(namespace != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	kv = g_slice_new(JKV);
	kv->index = j_helper_hash(key) % j_configuration_get_kv_server_count(configuration);
	kv->namespace = g_strdup(namespace);
	kv->key = g_strdup(key);
	kv->ref_count = 1;

	j_trace_leave(G_STRFUNC);

	return kv;
}

/**
 * Creates a new item.
 *
 * \author Michael Kuhn
 *
 * \code
 * JKV* i;
 *
 * i = j_kv_new("JULEA");
 * \endcode
 *
 * \param key         An item key.
 * \param distribution A distribution.
 *
 * \return A new item. Should be freed with j_kv_unref().
 **/
JKV*
j_kv_new_for_index (guint32 index, gchar const* namespace, gchar const* key)
{
	JConfiguration* configuration = j_configuration();
	JKV* kv;

	g_return_val_if_fail(namespace != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(index < j_configuration_get_kv_server_count(configuration), NULL);

	j_trace_enter(G_STRFUNC, NULL);

	kv = g_slice_new(JKV);
	kv->index = index;
	kv->namespace = g_strdup(namespace);
	kv->key = g_strdup(key);
	kv->ref_count = 1;

	j_trace_leave(G_STRFUNC);

	return kv;
}

/**
 * Increases an item's reference count.
 *
 * \author Michael Kuhn
 *
 * \code
 * JKV* i;
 *
 * j_kv_ref(i);
 * \endcode
 *
 * \param item An item.
 *
 * \return #item.
 **/
JKV*
j_kv_ref (JKV* kv)
{
	g_return_val_if_fail(kv != NULL, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	g_atomic_int_inc(&(kv->ref_count));

	j_trace_leave(G_STRFUNC);

	return kv;
}

/**
 * Decreases an item's reference count.
 * When the reference count reaches zero, frees the memory allocated for the item.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 **/
void
j_kv_unref (JKV* kv)
{
	g_return_if_fail(kv != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	if (g_atomic_int_dec_and_test(&(kv->ref_count)))
	{
		g_free(kv->key);
		g_free(kv->namespace);

		g_slice_free(JKV, kv);
	}

	j_trace_leave(G_STRFUNC);
}

/**
 * Creates an object.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param key         A key.
 * \param distribution A distribution.
 * \param batch        A batch.
 *
 * \return A new item. Should be freed with j_kv_unref().
 **/
void
j_kv_put (JKV* kv, bson_t* value, JBatch* batch)
{
	JKVOperation* kop;
	JOperation* operation;

	g_return_if_fail(kv != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	kop = g_slice_new(JKVOperation);
	kop->put.kv = j_kv_ref(kv);
	kop->put.value = value;

	operation = j_operation_new();
	// FIXME key = index + namespace
	operation->key = kv;
	operation->data = kop;
	operation->exec_func = j_kv_put_exec;
	operation->free_func = j_kv_put_free;

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Deletes an object.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item       An item.
 * \param batch      A batch.
 **/
void
j_kv_delete (JKV* object, JBatch* batch)
{
	JOperation* operation;

	g_return_if_fail(object != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	operation = j_operation_new();
	operation->key = object;
	operation->data = j_kv_ref(object);
	operation->exec_func = j_kv_delete_exec;
	operation->free_func = j_kv_delete_free;

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Get the status of an item.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item      An item.
 * \param batch     A batch.
 **/
void
j_kv_get (JKV* kv, bson_t* value, JBatch* batch)
{
	JKVOperation* kop;
	JOperation* operation;

	g_return_if_fail(kv != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	kop = g_slice_new(JKVOperation);
	kop->get.kv = j_kv_ref(kv);
	kop->get.value = value;
	kop->get.func = NULL;
	kop->get.data = NULL;

	operation = j_operation_new();
	operation->key = kv;
	operation->data = kop;
	operation->exec_func = j_kv_get_exec;
	operation->free_func = j_kv_get_free;

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Get the status of an item.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item      An item.
 * \param batch     A batch.
 **/
void
j_kv_get_callback (JKV* kv, JKVGetFunc func, gpointer data, JBatch* batch)
{
	JKVOperation* kop;
	JOperation* operation;

	g_return_if_fail(kv != NULL);
	g_return_if_fail(func != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	kop = g_slice_new(JKVOperation);
	kop->get.kv = j_kv_ref(kv);
	kop->get.value = NULL;
	kop->get.func = func;
	kop->get.data = data;

	operation = j_operation_new();
	operation->key = kv;
	operation->data = kop;
	operation->exec_func = j_kv_get_exec;
	operation->free_func = j_kv_get_free;

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * @}
 **/
