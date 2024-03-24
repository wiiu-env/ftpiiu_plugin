#pragma once

#include <mutex>
#include <string>
#include <sys/dirent.h>
#include <vector>

class IOAbstraction
{
public:
	static std::string convertPath (std::string_view inPath);

	static FILE *fopen (const char *_name, const char *_type);

	static int fseek (FILE *f, long pos, int origin);

	static size_t fread (void *buffer, size_t _size, size_t _n, FILE *f);

	static size_t fwrite (const void *buffer, size_t _size, size_t _n, FILE *f);

	static int closedir (DIR *dirp);

	static DIR *opendir (const char *dirname);

	static struct dirent *readdir (DIR *dirp);

	static int stat (const char *path, struct stat *sbuf);

	static int lstat (const char *path, struct stat *buf);

	static int mkdir (const char *path, mode_t mode);

	static int rmdir (const char *path);

	static int rename (const char *path, const char *path2);

	static int unlink (const char *path);

	static void addVirtualPath (const std::string &virtualPath,
	    const std::vector<std::string> &subDirectories);

	static void clear ();
};
