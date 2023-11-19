#include "IOAbstraction.h"
#include <algorithm>
#include <map>
#include <memory>
#include <sys/dirent.h>
#include <vector>

class VirtualDirectory
{
public:
	VirtualDirectory (const std::vector<std::string> &directories)
	{
		mDirectories.push_back (".");
		mDirectories.push_back ("..");
		mDirectories.insert (mDirectories.end (), directories.begin (), directories.end ());

		mCurIterator = mDirectories.begin ();
	}

	[[nodiscard]] DIR *getAsDir () const
	{
		return (DIR *)this;
	}

	struct dirent *readdir ()
	{
		if (mCurIterator == mDirectories.end ())
		{
			return nullptr;
		}
		mDir = {};
		snprintf (mDir.d_name, sizeof (mDir.d_name), "%s", mCurIterator->c_str ());
		mCurIterator++;
		return &mDir;
	}

private:
	std::vector<std::string> mDirectories;
	struct dirent mDir = {};
	std::vector<std::string>::iterator mCurIterator{};
};

std::vector<std::unique_ptr<VirtualDirectory>> sOpenVirtualDirectories;
std::mutex sOpenVirtualDirectoriesMutex;
std::map<std::string, std::vector<std::string>> sVirtualDirs;

template <typename Container, typename Predicate>
typename std::enable_if<std::is_same<Container, std::vector<typename Container::value_type>>::value,
    bool>::type
    remove_first_if (Container &container, Predicate pred)
{
	auto it = container.begin ();
	while (it != container.end ())
	{
		if (pred (*it))
		{
			container.erase (it);
			return true;
		}
		++it;
	}

	return false;
}

template <typename Container, typename Predicate>
bool remove_locked_first_if (std::mutex &mutex, Container &container, Predicate pred)
{
	std::lock_guard<std::mutex> lock (mutex);
	return remove_first_if (container, pred);
}

static DIR *getVirtualDir (const std::vector<std::string> &subDirectories)
{
	auto virtDir = std::make_unique<VirtualDirectory> (subDirectories);
	auto *result = virtDir->getAsDir ();
	std::lock_guard lock (sOpenVirtualDirectoriesMutex);
	sOpenVirtualDirectories.push_back (std::move (virtDir));
	return result;
}

std::string IOAbstraction::convertPath (std::string_view inPath)
{
#ifdef __WIIU__
	if (!inPath.starts_with ('/') || inPath.find (':') != std::string::npos)
	{
		return std::string (inPath);
	}
	std::string path      = std::string (inPath);
	size_t secondSlashPos = path.find ('/', 1);
	if (secondSlashPos != std::string::npos)
	{
		// Extract the substring between the first and second slashes
		std::string prefix = path.substr (1, secondSlashPos - 1);
		std::string suffix = path.substr (secondSlashPos);

		// Concatenate the modified prefix and suffix
		path = prefix + ":" + suffix;
	}
	else
	{
		path = std::string (inPath.substr (1)) + ":/";
	}
	return path;
#else
	return std::string (inPath);
#endif
}

int IOAbstraction::closedir (DIR *dirp)
{
	{
		std::lock_guard lock (sOpenVirtualDirectoriesMutex);
		if (remove_locked_first_if (sOpenVirtualDirectoriesMutex,
		        sOpenVirtualDirectories,
		        [dirp] (auto &cur) { return cur->getAsDir () == dirp; }))
		{
			return 0;
		}
	}
	return ::closedir (dirp);
}

DIR *IOAbstraction::opendir (const char *dirname)
{
	auto convertedPath = convertPath (dirname);
	if (sVirtualDirs.count (convertedPath) > 0)
	{
		return getVirtualDir (sVirtualDirs[convertedPath]);
	}

	return ::opendir (convertedPath.c_str ());
}

FILE *IOAbstraction::fopen (const char *_name, const char *_type)
{
	return std::fopen (convertPath (_name).c_str (), _type);
}

int IOAbstraction::fseek (FILE *f, long pos, int origin)
{
	return std::fseek (f, pos, origin);
}

size_t IOAbstraction::fread (void *buffer, size_t _size, size_t _n, FILE *f)
{
	return std::fread (buffer, _size, _n, f);
}

size_t IOAbstraction::fwrite (const void *buffer, size_t _size, size_t _n, FILE *f)
{
	return std::fwrite (buffer, _size, _n, f);
}

struct dirent *IOAbstraction::readdir (DIR *dirp)
{
	{
		std::lock_guard lock (sOpenVirtualDirectoriesMutex);
		auto itr = std::find_if (sOpenVirtualDirectories.begin (),
		    sOpenVirtualDirectories.end (),
		    [dirp] (auto &cur) { return cur->getAsDir () == dirp; });
		if (itr != sOpenVirtualDirectories.end ())
		{
			return (*itr)->readdir ();
		}
	}

	return ::readdir (dirp);
}

int IOAbstraction::stat (const char *path, struct stat *sbuf)
{
	auto convertedPath = convertPath (path);
	auto r             = ::stat (convertedPath.c_str (), sbuf);
	if (r < 0)
	{
		if (sVirtualDirs.contains (convertedPath))
		{
			*sbuf = {};
			// TODO: init other values?
			sbuf->st_mode = _IFDIR;
			return 0;
		}
	}
	return r;
}

int IOAbstraction::lstat (const char *path, struct stat *buf)
{
	return IOAbstraction::stat (path, buf);
}

void IOAbstraction::addVirtualPath (const std::string &virtualPath,
    const std::vector<std::string> &subDirectories)
{
	sVirtualDirs.insert (std::make_pair (virtualPath, subDirectories));
}
