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
#include <string.h>

int
jfs_getattr (char const* path, struct stat* stbuf)
{
	int ret = -ENOENT;

	g_autoptr(JBatch) batch = NULL;
	g_autoptr(JKV) kv = NULL;
	bson_t file[1];

	if (g_strcmp0(path, "/") == 0)
	{
		stbuf->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		stbuf->st_nlink = 1;
		stbuf->st_uid = 0;
		stbuf->st_gid = 0;
		stbuf->st_size = 0;
		stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = 0;

		return 0;
	}

	batch = j_batch_new_for_template(J_SEMANTICS_TEMPLATE_POSIX);
	kv = j_kv_new("posix", path);

	j_kv_get(kv, file, batch);

	if (j_batch_execute(batch))
	{
		bson_iter_t iter;
		gboolean is_file = TRUE;
		// FIXME
		gint64 size = 0;
		gint64 time = 0;

		if (bson_iter_init_find(&iter, file, "file") && bson_iter_type(&iter) == BSON_TYPE_BOOL)
		{
			is_file = bson_iter_bool(&iter);
		}

		if (is_file)
		{
			stbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
			stbuf->st_nlink = 1;
			stbuf->st_uid = 0;
			stbuf->st_gid = 0;
			stbuf->st_size = size;
			stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = time / G_USEC_PER_SEC;

			ret = 0;
		}
		else
		{
			stbuf->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
			stbuf->st_nlink = 1;
			stbuf->st_uid = 0;
			stbuf->st_gid = 0;
			stbuf->st_size = 0;
			stbuf->st_atime = stbuf->st_ctime = stbuf->st_mtime = 0;

			ret = 0;
		}
	}

	return ret;
}
