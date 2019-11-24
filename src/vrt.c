/*

Copyright (C) 2008 Joseph Jordan <joe.ftpii@psychlaw.com.au>
This work is derived from Daniel Ehlers' <danielehlers@mindeye.net> srg_vrt branch.

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1.The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software in a
product, an acknowledgment in the product documentation would be
appreciated but is not required.

2.Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3.This notice may not be removed or altered from any source distribution.

*/
#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <string.h>
#include <sys/dirent.h>
#include <unistd.h>
#include "main.h"

#include "virtualpath.h"
#include "vrt.h"

static char *virtual_abspath(char *virtual_cwd, char *virtual_path) {
	char *path;
	if (virtual_path[0] == '/') {
		path = virtual_path;
	} else {
		size_t path_size = strlen(virtual_cwd) + strlen(virtual_path) + 1;
		if (path_size > MAXPATHLEN || !(path = malloc(path_size))) return NULL;
		strcpy(path, virtual_cwd);
		strcat(path, virtual_path);
	}

	char *normalised_path = malloc(strlen(path) + 1);
	if (!normalised_path) goto end;
	*normalised_path = '\0';
	char *curr_dir = normalised_path;

	uint32_t state = 0; // 0:start, 1:slash, 2:dot, 3:dotdot
	char *token = path;
	while (1) {
		switch (state) {
		case 0:
			if (*token == '/') {
				state = 1;
				curr_dir = normalised_path + strlen(normalised_path);
				strncat(normalised_path, token, 1);
			}
			break;
		case 1:
			if (*token == '.') state = 2;
			else if (*token != '/') state = 0;
			break;
		case 2:
			if (*token == '/' || !*token) {
				state = 1;
				*(curr_dir + 1) = '\0';
			} else if (*token == '.') state = 3;
			else state = 0;
			break;
		case 3:
			if (*token == '/' || !*token) {
				state = 1;
				*curr_dir = '\0';
				char *prev_dir = strrchr(normalised_path, '/');
				if (prev_dir) curr_dir = prev_dir;
				else *curr_dir = '/';
				*(curr_dir + 1) = '\0';
			} else state = 0;
			break;
		}
		if (!*token) break;
		if (state == 0 || *token != '/') strncat(normalised_path, token, 1);
		token++;
	}

	uint32_t end = strlen(normalised_path);
	while (end > 1 && normalised_path[end - 1] == '/') {
		normalised_path[--end] = '\x00';
	}

	end:
	if (path != virtual_path) free(path);
	return normalised_path;
}

/*
	Converts a client-visible path to a real absolute path
	E.g. "/sd/foo"	-> "sd:/foo"
		 "/sd"		-> "sd:/"
		 "/sd/../usb" -> "usb:/"
	The resulting path will fit in an array of size MAXPATHLEN
	Returns NULL to indicate that the client-visible path is invalid
*/
char *to_real_path(char *virtual_cwd, char *virtual_path) {
	errno = ENOENT;
	if (strchr(virtual_path, ':')) {
		return NULL; // colon is not allowed in virtual path, i've decided =P
	}

	virtual_path = virtual_abspath(virtual_cwd, virtual_path);
	if (!virtual_path) return NULL;

	char *path = NULL;
	char *rest = virtual_path;

	if (!strcmp("/", virtual_path)) {
		// indicate vfs-root with ""
		path = "";
		goto end;
	}

	const char *prefix = NULL;
	uint32_t i;
	for (i = 0; i < MAX_VIRTUAL_PARTITIONS; i++) {
		VIRTUAL_PARTITION *partition = VIRTUAL_PARTITIONS + i;
		const char *alias = partition->alias;
		size_t alias_len = strlen(alias);
		if (!strcasecmp(alias, virtual_path) || (!strncasecmp(alias, virtual_path, alias_len) && virtual_path[alias_len] == '/')) {
			prefix = partition->prefix;
			rest += alias_len;
			if (*rest == '/') rest++;
			break;
		}
	}
	if (!prefix) {
		errno = ENODEV;
		goto end;
	}

	size_t real_path_size = strlen(prefix) + strlen(rest) + 1;
	if (real_path_size > MAXPATHLEN) goto end;

	path = malloc(real_path_size);
	if (!path) goto end;
	strcpy(path, prefix);
	strcat(path, rest);

	end:
	free(virtual_path);
	return path;
}

static int checkdir(char *path) {
	DIR *dir = opendir(path);
	if(dir)
	{
		closedir(dir);
		return 0;
	}
	return -1;
}

typedef void * (*path_func)(char *path, ...);

static void *with_virtual_path(void *virtual_cwd, void *void_f, char *virtual_path, int32_t failed, ...) {
	char *path = to_real_path(virtual_cwd, virtual_path);
	if (!path || !*path) return (void *)failed;

	path_func f = (path_func)void_f;
	va_list ap;
	void *args[3];
	unsigned int num_args = 0;
	va_start(ap, failed);
	do {
		void *arg = va_arg(ap, void *);
		if (!arg) break;
		args[num_args++] = arg;
	} while (1);
	va_end(ap);

	void *result;
	switch (num_args) {
		case 0: result = f(path); break;
		case 1: result = f(path, args[0]); break;
		case 2: result = f(path, args[0], args[1]); break;
		case 3: result = f(path, args[0], args[1], args[2]); break;
		default: result = (void *)failed; break;
	}

	free(path);
	return result;
}

FILE *vrt_fopen(char *cwd, char *path, char *mode) {
	return with_virtual_path(cwd, fopen, path, 0, mode, NULL);
}

int vrt_stat(char *cwd, char *path, struct stat *st) {
	char *real_path = to_real_path(cwd, path);
	if (!real_path)
    {
        return -1;
    }
	else if (!*real_path || (strcmp(path, ".") == 0) || (strlen(cwd) == 1) || ((strlen(cwd) > 1) && (strcmp(path, "..") == 0)))
    {
		st->st_mode = S_IFDIR;
		st->st_size = 31337;
		return 0;
	}
	free(real_path);
	return (int)with_virtual_path(cwd, stat, path, -1, st, NULL);
}

static int vrt_checkdir(char *cwd, char *path) {
	char *real_path = to_real_path(cwd, path);
	if (!real_path)
	{
		return -1;
	}
	else if (!*real_path || (strcmp(path, ".") == 0) || (strlen(cwd) == 1) || ((strlen(cwd) > 1) && (strcmp(path, "..") == 0)))
	{
		return 0;
	}
	free(real_path);
	return (int)with_virtual_path(cwd, checkdir, path, -1, NULL);
}

int vrt_chdir(char *cwd, char *path) {

	if (vrt_checkdir(cwd, path)) {
		return -1;
	}
	char *abspath = virtual_abspath(cwd, path);
	if (!abspath) {
		errno = ENOMEM;
		return -1;
	}
	strcpy(cwd, abspath);
	if (cwd[1]) strcat(cwd, "/");
	free(abspath);
	return 0;
}

int vrt_unlink(char *cwd, char *path) {
	return (int)with_virtual_path(cwd, unlink, path, -1, NULL);
}

int vrt_mkdir(char *cwd, char *path, mode_t mode) {
	return (int)with_virtual_path(cwd, mkdir, path, -1, mode, NULL);
}

int vrt_rename(char *cwd, char *from_path, char *to_path) {
	char *real_to_path = to_real_path(cwd, to_path);
	if (!real_to_path || !*real_to_path) return -1;
	int result = (int)with_virtual_path(cwd, rename, from_path, -1, real_to_path, NULL);
	free(real_to_path);
	return result;
}

/*
	When in vfs-root this creates a fake DIR_ITER.
 */
DIR_P *vrt_opendir(char *cwd, char *path)
{
	char *real_path = to_real_path(cwd, path);
	if (!real_path) return NULL;

	DIR_P *iter = malloc(sizeof(DIR_P));
	if (!iter)
	{
		if (*real_path != 0)
			free(real_path);
		return NULL;
	}

	iter->virt_root = 0;
	iter->path = real_path;

	if (*iter->path == 0) {
		iter->dir = malloc(sizeof(DIR));
		if(!iter->dir) {
			// root path is not allocated
			free(iter);
			return NULL;
		}
		memset(iter->dir, 0, sizeof(DIR));
		iter->virt_root = 1; // we are at the virtual root
		return iter;
	}

	iter->dir = with_virtual_path(cwd, opendir, path, 0, NULL);
	if(!iter->dir)
	{
		free(iter->path);
		free(iter);
		return NULL;
	}

	return iter;
}

/*
	Yields virtual aliases when pDir->virt_root
 */
struct dirent *vrt_readdir(DIR_P *pDir) {
	if(!pDir || !pDir->dir) return NULL;

	DIR *iter = pDir->dir;
	if (pDir->virt_root) {
		for (; (uint32_t)iter->position < MAX_VIRTUAL_PARTITIONS; iter->position++) {
			VIRTUAL_PARTITION *partition = VIRTUAL_PARTITIONS + (int)iter->position;
			if (partition->inserted) {
				iter->fileData.d_type = DT_DIR;
				strcpy(iter->fileData.d_name, partition->alias + 1);
				iter->position++;
				return &iter->fileData;
			}
		}
		return NULL;
	}
	return readdir(iter);
}

int vrt_closedir(DIR_P *iter) {
	if(!iter) return -1;

	if(iter->dir)
	{
		if (iter->virt_root)
			free(iter->dir);
		else
			closedir(iter->dir);
	}

	// root path is not allocated
	if(iter->path && *iter->path != 0)
		free(iter->path);

	free(iter);

	return 0;
}
