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

#include <julea-config.h>

#include "julea-fuse.h"

#include <errno.h>

int jfs_create (char const* path, mode_t mode, struct fuse_file_info* fi)
{
	int ret = -ENOENT;

	g_autoptr(JBatch) batch = NULL;
	g_autoptr(JKV) kv = NULL;
	g_autoptr(JObject) object = NULL;
	bson_t* file;
	g_autofree gchar* basename = NULL;

	(void)mode;
	(void)fi;

	basename = g_path_get_basename(path);
	batch = j_batch_new_for_template(J_SEMANTICS_TEMPLATE_POSIX);
	kv = j_kv_new("posix", path);
	object = j_object_new("posix", path);

	// FIXME
	file = g_slice_new(bson_t);
	bson_init(file);
	bson_append_utf8(file, "name", -1, basename, -1);
	bson_append_bool(file, "file", -1, TRUE);
	bson_append_int64(file, "size", -1, 0);
	bson_append_int64(file, "time", -1, g_get_real_time());

	j_kv_put(kv, file, batch);
	j_object_create(object, batch);

	if (j_batch_execute(batch))
	{
		ret = 0;
	}

	return ret;
}
