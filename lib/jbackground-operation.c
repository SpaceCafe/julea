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

#include <julea-config.h>

#include <glib.h>

#include <unistd.h>

#include <jbackground-operation.h>
#include <jbackground-operation-internal.h>

#include <jcommon.h>
#include <jhelper-internal.h>
#include <jtrace-internal.h>

/**
 * \defgroup JBackgroundOperation Background Operation
 * @{
 **/

/**
 * A background operation.
 **/
struct JBackgroundOperation
{
	/**
	 * The function to execute in the background.
	 **/
	JBackgroundOperationFunc func;

	/**
	 * User data to give to #func.
	 **/
	gpointer data;

	/**
	 * The return value of #func.
	 **/
	gpointer result;

	/**
	 * Whether the background operation has finished.
	 **/
	gboolean completed;

	/**
	 * The mutex for #completed.
	 */
	GMutex mutex[1];

	/**
	 * The condition for #completed.
	 */
	GCond cond[1];

	/**
	 * The reference count.
	 **/
	gint ref_count;
};

static GThreadPool* j_thread_pool = NULL;

/**
 * Executes background operations.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param data A background operations.
 * \param user_data User data.
 **/
static
void
j_background_operation_thread (gpointer data, gpointer user_data)
{
	JBackgroundOperation* background_operation = data;

	(void)user_data;

	j_trace_enter(G_STRFUNC, NULL);

	background_operation->result = (*(background_operation->func))(background_operation->data);

	g_mutex_lock(background_operation->mutex);
	background_operation->completed = TRUE;
	g_cond_signal(background_operation->cond);
	g_mutex_unlock(background_operation->mutex);

	j_background_operation_unref(background_operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Initializes the background operation framework.
 *
 * \author Michael Kuhn
 *
 * \code
 * j_background_operation_init();
 * \endcode
 **/
void
j_background_operation_init (guint count)
{
	GThreadPool* thread_pool;

	g_return_if_fail(j_thread_pool == NULL);

	j_trace_enter(G_STRFUNC, NULL);

	if (count == 0)
	{
		count = g_get_num_processors();
	}

	thread_pool = g_thread_pool_new(j_background_operation_thread, NULL, count, FALSE, NULL);
	g_atomic_pointer_set(&j_thread_pool, thread_pool);

	j_trace_leave(G_STRFUNC);
}

/**
 * Shuts down the background operation framework.
 *
 * \author Michael Kuhn
 *
 * \code
 * j_background_operation_fini();
 * \endcode
 **/
void
j_background_operation_fini (void)
{
	GThreadPool* thread_pool;

	g_return_if_fail(j_thread_pool != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	thread_pool = g_atomic_pointer_get(&j_thread_pool);
	g_atomic_pointer_set(&j_thread_pool, NULL);

	g_thread_pool_free(thread_pool, FALSE, TRUE);

	j_trace_leave(G_STRFUNC);
}

guint
j_background_operation_get_num_threads (void)
{
	return g_thread_pool_get_max_threads(j_thread_pool);
}

/**
 * Creates a new background operation.
 *
 * \author Michael Kuhn
 *
 * \code
 * static
 * gpointer
 * background_func (gpointer data)
 * {
 *   return NULL;
 * }
 *
 * JBackgroundOperation* background_operation;
 *
 * background_operation = j_background_operation_new(background_func, NULL);
 * \endcode
 *
 * \param func A function to execute in the background.
 * \param data User data given to #func.
 *
 * \return A new background operation. Should be freed with j_background_operation_unref().
 **/
JBackgroundOperation*
j_background_operation_new (JBackgroundOperationFunc func, gpointer data)
{
	JBackgroundOperation* background_operation;

	g_return_val_if_fail(func != NULL, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	background_operation = g_slice_new(JBackgroundOperation);
	background_operation->func = func;
	background_operation->data = data;
	background_operation->result = NULL;
	background_operation->completed = FALSE;
	background_operation->ref_count = 2;

	g_mutex_init(background_operation->mutex);
	g_cond_init(background_operation->cond);

	g_thread_pool_push(j_thread_pool, background_operation, NULL);

	j_trace_leave(G_STRFUNC);

	return background_operation;
}

/**
 * Increases a background operation's reference count.
 *
 * \author Michael Kuhn
 *
 * \code
 * JBackgroundOperation* background_operation;
 *
 * j_background_operation_ref(background_operation);
 * \endcode
 *
 * \param background_operation A background operation.
 *
 * \return #background_operation.
 **/
JBackgroundOperation*
j_background_operation_ref (JBackgroundOperation* background_operation)
{
	g_return_val_if_fail(background_operation != NULL, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	g_atomic_int_inc(&(background_operation->ref_count));

	j_trace_leave(G_STRFUNC);

	return background_operation;
}

/**
 * Decreases a background operation's reference count.
 * When the reference count reaches zero, frees the memory allocated for the background operation.
 *
 * \author Michael Kuhn
 *
 * \code
 * JBackgroundOperation* background_operation;
 *
 * j_background_operation_unref(background_operation);
 * \endcode
 *
 * \param background_operation A background operation.
 **/
void
j_background_operation_unref (JBackgroundOperation* background_operation)
{
	g_return_if_fail(background_operation != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	if (g_atomic_int_dec_and_test(&(background_operation->ref_count)))
	{
		g_cond_clear(background_operation->cond);
		g_mutex_clear(background_operation->mutex);

		g_slice_free(JBackgroundOperation, background_operation);
	}

	j_trace_leave(G_STRFUNC);
}

/**
 * Waits for a background operation to finish.
 *
 * \author Michael Kuhn
 *
 * \code
 * JBackgroundOperation* background_operation;
 *
 * j_background_operation_wait(background_operation);
 * \endcode
 *
 * \param background_operation A background operation.
 *
 * \return The return value of the function given to j_background_operation_new().
 **/
gpointer
j_background_operation_wait (JBackgroundOperation* background_operation)
{
	g_return_val_if_fail(background_operation != NULL, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	g_mutex_lock(background_operation->mutex);

	while (!background_operation->completed)
	{
		g_cond_wait(background_operation->cond, background_operation->mutex);
	}

	g_mutex_unlock(background_operation->mutex);

	j_trace_leave(G_STRFUNC);

	return background_operation->result;
}

/**
 * @}
 **/
