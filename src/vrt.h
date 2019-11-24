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
#ifndef _VRT_H_
#define _VRT_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdio.h>
#include <sys/dirent.h>

typedef struct
{
	DIR *dir;
	char *path;
	uint8_t virt_root;
} DIR_P;

char *to_real_path(char *virtual_cwd, char *virtual_path);

FILE *vrt_fopen(char *cwd, char *path, char *mode);
int vrt_stat(char *cwd, char *path, struct stat *st);
int vrt_chdir(char *cwd, char *path);
int vrt_unlink(char *cwd, char *path);
int vrt_mkdir(char *cwd, char *path, mode_t mode);
int vrt_rename(char *cwd, char *from_path, char *to_path);
DIR_P *vrt_opendir(char *cwd, char *path);
struct dirent *vrt_readdir(DIR_P *iter);
int vrt_closedir(DIR_P *iter);

#ifdef __cplusplus
}
#endif

#endif /* _VRT_H_ */
