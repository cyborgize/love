/**
 * Copyright (c) 2006-2014 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#include <iostream>
#include <sstream>

#include "common/utf8.h"
#include "common/b64.h"

#include "Filesystem.h"
#include "File.h"

// PhysFS
#ifdef LOVE_MACOSX_USE_FRAMEWORKS
#include <physfs/physfs.h>
#else
#include <physfs.h>
#endif

// For great CWD. (Current Working Directory)
// Using this instead of boost::filesystem which totally
// cramped our style.
#ifdef LOVE_WINDOWS
#	include <windows.h>
#	include <direct.h>
#else
#	include <sys/param.h>
#	include <unistd.h>
#endif

namespace
{
	size_t getDriveDelim(const std::string &input)
	{
		for (size_t i = 0; i < input.size(); ++i)
			if (input[i] == '/' || input[i] == '\\')
				return i;
		// Something's horribly wrong
		return 0;
	}

	std::string getDriveRoot(const std::string &input)
	{
		return input.substr(0, getDriveDelim(input)+1);
	}

	std::string skipDriveRoot(const std::string &input)
	{
		return input.substr(getDriveDelim(input)+1);
	}

	std::string normalize(const std::string &input)
	{
		std::stringstream out;
		bool seenSep = false, isSep = false;
		for (size_t i = 0; i < input.size(); ++i)
		{
			isSep = (input[i] == LOVE_PATH_SEPARATOR[0]);
			if (!isSep || !seenSep)
				out << input[i];
			seenSep = isSep;
		}

		return out.str();
	}
}

namespace love
{
namespace filesystem
{
namespace physfs
{

Filesystem::Filesystem()
	: fused(false)
	, fusedSet(false)
{
	requirePath = {"?.lua", "?/init.lua"};
}

Filesystem::~Filesystem()
{
	if (PHYSFS_isInit())
		PHYSFS_deinit();
}

const char *Filesystem::getName() const
{
	return "love.filesystem.physfs";
}

void Filesystem::init(const char *arg0)
{
	if (!PHYSFS_init(arg0))
		throw Exception(PHYSFS_getLastError());
}

void Filesystem::setFused(bool fused)
{
	if (fusedSet)
		return;
	this->fused = fused;
	fusedSet = true;
}

bool Filesystem::isFused() const
{
	if (!fusedSet)
		return false;
	return fused;
}

bool Filesystem::setIdentity(const char *ident, bool appendToPath)
{
	if (!PHYSFS_isInit())
		return false;

	std::string old_save_path = save_path_full;

	// Store the save directory.
	save_identity = std::string(ident);

	// Generate the relative path to the game save folder.
	save_path_relative = std::string(LOVE_APPDATA_PREFIX LOVE_APPDATA_FOLDER LOVE_PATH_SEPARATOR) + save_identity;

	// Generate the full path to the game save folder.
	save_path_full = std::string(getAppdataDirectory()) + std::string(LOVE_PATH_SEPARATOR);
	if (fused)
		save_path_full += std::string(LOVE_APPDATA_PREFIX) + save_identity;
	else
		save_path_full += save_path_relative;

	save_path_full = normalize(save_path_full);

	// We now have something like:
	// save_identity: game
	// save_path_relative: ./LOVE/game
	// save_path_full: C:\Documents and Settings\user\Application Data/LOVE/game

	// We don't want old read-only save paths to accumulate when we set a new
	// identity.
	if (!old_save_path.empty())
		PHYSFS_removeFromSearchPath(old_save_path.c_str());

	// Try to add the save directory to the search path.
	// (No error on fail, it means that the path doesn't exist).
	PHYSFS_addToSearchPath(save_path_full.c_str(), appendToPath);

	// HACK: This forces setupWriteDirectory to be called the next time a file
	// is opened for writing - otherwise it won't be called at all if it was
	// already called at least once before.
	PHYSFS_setWriteDir(nullptr);

	return true;
}

const char *Filesystem::getIdentity() const
{
	return save_identity.c_str();
}

bool Filesystem::setSource(const char *source)
{
	if (!PHYSFS_isInit())
		return false;

	// Check whether directory is already set.
	if (!game_source.empty())
		return false;

	// Add the directory.
	if (!PHYSFS_addToSearchPath(source, 1))
		return false;

	// Save the game source.
	game_source = std::string(source);

	return true;
}

const char *Filesystem::getSource() const
{
	return game_source.c_str();
}

bool Filesystem::setupWriteDirectory()
{
	if (!PHYSFS_isInit())
		return false;

	// These must all be set.
	if (save_identity.empty() || save_path_full.empty() || save_path_relative.empty())
		return false;

	// We need to make sure the write directory is created. To do that, we also
	// need to make sure all its parent directories are also created.
	std::string temp_writedir = getDriveRoot(save_path_full);
	std::string temp_createdir = skipDriveRoot(save_path_full);

	// On some sandboxed platforms, physfs will break when its write directory
	// is the root of the drive and it tries to create a folder (even if the
	// folder's path is in a writable location.) If the user's home folder is
	// in the save path, we'll try starting from there instead.
	if (save_path_full.find(getUserDirectory()) == 0)
	{
		temp_writedir = getUserDirectory();
		temp_createdir = save_path_full.substr(getUserDirectory().length());

		// Strip leading '/' characters from the path we want to create.
		size_t startpos = temp_createdir.find_first_not_of('/');
		if (startpos != std::string::npos)
			temp_createdir = temp_createdir.substr(startpos);
	}

	// Set either '/' or the user's home as a writable directory.
	// (We must create the save folder before mounting it).
	if (!PHYSFS_setWriteDir(temp_writedir.c_str()))
		return false;

	// Create the save folder. (We're now "at" either '/' or the user's home).
	if (!createDirectory(temp_createdir.c_str()))
	{
		// Clear the write directory in case of error.
		PHYSFS_setWriteDir(nullptr);
		return false;
	}

	// Set the final write directory.
	if (!PHYSFS_setWriteDir(save_path_full.c_str()))
		return false;

	// Add the directory. (Will not be readded if already present).
	if (!PHYSFS_addToSearchPath(save_path_full.c_str(), 0))
	{
		PHYSFS_setWriteDir(nullptr); // Clear the write directory in case of error.
		return false;
	}

	return true;
}

bool Filesystem::mount(const char *archive, const char *mountpoint, bool appendToPath)
{
	if (!PHYSFS_isInit() || !archive)
		return false;

	std::string realPath;
	std::string sourceBase = getSourceBaseDirectory();

	// Check whether the given archive path is in the list of allowed full paths.
	auto it = std::find(allowedMountPaths.begin(), allowedMountPaths.end(), archive);

	if (it != allowedMountPaths.end())
	{
		realPath = *it;
	}
	else if (isFused() && sourceBase.compare(archive) == 0)
	{
		// Special case: if the game is fused and the archive is the source's
		// base directory, mount it even though it's outside of the save dir.
		realPath = sourceBase;
	}
	else
	{
		// Not allowed for safety reasons.
		if (strlen(archive) == 0 || strstr(archive, "..") || strcmp(archive, "/") == 0)
			return false;

		const char *realDir = PHYSFS_getRealDir(archive);
		if (!realDir)
			return false;

		realPath = realDir;

		// Always disallow mounting of files inside the game source, since it
		// won't work anyway if the game source is a zipped .love file.
		if (realPath.find(game_source) == 0)
			return false;

		realPath += LOVE_PATH_SEPARATOR;
		realPath += archive;
	}

	if (realPath.length() == 0)
		return false;

	return PHYSFS_mount(realPath.c_str(), mountpoint, appendToPath);
}

bool Filesystem::unmount(const char *archive)
{
	if (!PHYSFS_isInit() || !archive)
		return false;

	std::string realPath;
	std::string sourceBase = getSourceBaseDirectory();

	if (isFused() && sourceBase.compare(archive) == 0)
	{
		// Special case: if the game is fused and the archive is the source's
		// base directory, unmount it even though it's outside of the save dir.
		realPath = sourceBase;
	}
	else
	{
		// Not allowed for safety reasons.
		if (strlen(archive) == 0 || strstr(archive, "..") || strcmp(archive, "/") == 0)
			return false;

		const char *realDir = PHYSFS_getRealDir(archive);
		if (!realDir)
			return false;

		realPath = realDir;
		realPath += LOVE_PATH_SEPARATOR;
		realPath += archive;
	}

	const char *mountPoint = PHYSFS_getMountPoint(realPath.c_str());
	if (!mountPoint)
		return false;

	return PHYSFS_removeFromSearchPath(realPath.c_str());
}

love::filesystem::File *Filesystem::newFile(const char *filename) const
{
	return new File(filename);
}

FileData *Filesystem::newFileData(void *data, unsigned int size, const char *filename) const
{
	FileData *fd = new FileData(size, std::string(filename));

	// Copy the data into FileData.
	memcpy(fd->getData(), data, size);

	return fd;
}

FileData *Filesystem::newFileData(const char *b64, const char *filename) const
{
	int size = strlen(b64);
	int outsize = 0;
	char *dst = b64_decode(b64, size, outsize);
	FileData *fd = new FileData(outsize, std::string(filename));

	// Copy the data into FileData.
	memcpy(fd->getData(), dst, outsize);
	delete [] dst;

	return fd;
}

const char *Filesystem::getWorkingDirectory()
{
	if (cwd.empty())
	{
#ifdef LOVE_WINDOWS

		WCHAR w_cwd[LOVE_MAX_PATH];
		_wgetcwd(w_cwd, LOVE_MAX_PATH);
		cwd = to_utf8(w_cwd);
		replace_char(cwd, '\\', '/');
#else
		char *cwd_char = new char[LOVE_MAX_PATH];

		if (getcwd(cwd_char, LOVE_MAX_PATH))
			cwd = cwd_char; // if getcwd fails, cwd_char (and thus cwd) will still be empty

		delete [] cwd_char;
#endif
	}

	return cwd.c_str();
}

std::string Filesystem::getUserDirectory()
{
	static std::string userDir = normalize(PHYSFS_getUserDir());
	return userDir;
}

std::string Filesystem::getAppdataDirectory()
{
	if (appdata.empty())
	{
#ifdef LOVE_WINDOWS
		wchar_t *w_appdata = _wgetenv(L"APPDATA");
		appdata = to_utf8(w_appdata);
		replace_char(appdata, '\\', '/');
#elif defined(LOVE_MACOSX)
		std::string udir = getUserDirectory();
		udir.append("/Library/Application Support");
		appdata = normalize(udir);
#elif defined(LOVE_LINUX)
		char *xdgdatahome = getenv("XDG_DATA_HOME");
		if (!xdgdatahome)
			appdata = normalize(std::string(getUserDirectory()) + "/.local/share/");
		else
			appdata = xdgdatahome;
#else
		appdata = getUserDirectory();
#endif
	}
	return appdata;
}


const char *Filesystem::getSaveDirectory()
{
	return save_path_full.c_str();
}

std::string Filesystem::getSourceBaseDirectory() const
{
	size_t source_len = game_source.length();

	if (source_len == 0)
		return "";

	// FIXME: This doesn't take into account parent and current directory
	// symbols (i.e. '..' and '.')
#ifdef LOVE_WINDOWS
	// In windows, delimiters can be either '/' or '\'.
	size_t base_end_pos = game_source.find_last_of("/\\", source_len - 2);
#else
	size_t base_end_pos = game_source.find_last_of('/', source_len - 2);
#endif

	if (base_end_pos == std::string::npos)
		return "";

	// If the source is in the unix root (aka '/'), we want to keep the '/'.
	if (base_end_pos == 0)
		base_end_pos = 1;

	return game_source.substr(0, base_end_pos);
}

std::string Filesystem::getRealDirectory(const char *filename) const
{
	const char *dir = PHYSFS_getRealDir(filename);

	if (dir == nullptr)
		throw love::Exception("File does not exist.");

	return std::string(dir);
}

bool Filesystem::isDirectory(const char *dir) const
{
	return PHYSFS_isDirectory(dir);
}

bool Filesystem::isFile(const char *file) const
{
	return PHYSFS_exists(file) && !PHYSFS_isDirectory(file);
}

bool Filesystem::createDirectory(const char *dir)
{
	if (PHYSFS_getWriteDir() == 0 && !setupWriteDirectory())
		return false;

	if (!PHYSFS_mkdir(dir))
		return false;
	return true;
}

bool Filesystem::remove(const char *file)
{
	if (PHYSFS_getWriteDir() == 0 && !setupWriteDirectory())
		return false;

	if (!PHYSFS_delete(file))
		return false;
	return true;
}

FileData *Filesystem::read(const char *filename, int64 size) const
{
	File file(filename);

	file.open(File::MODE_READ);

	// close() is called in the File destructor.
	return file.read(size);
}

void Filesystem::write(const char *filename, const void *data, int64 size) const
{
	File file(filename);

	file.open(File::MODE_WRITE);

	// close() is called in the File destructor.
	if (!file.write(data, size))
		throw love::Exception("Data could not be written.");
}

void Filesystem::append(const char *filename, const void *data, int64 size) const
{
	File file(filename);

	file.open(File::MODE_APPEND);

	// close() is called in the File destructor.
	if (!file.write(data, size))
		throw love::Exception("Data could not be written.");
}

int Filesystem::getDirectoryItems(lua_State *L)
{
	const char *dir = luaL_checkstring(L, 1);
	bool hascallback = !lua_isnoneornil(L, 2);

	if (hascallback)
		luaL_checktype(L, 2, LUA_TFUNCTION);

	char **rc = PHYSFS_enumerateFiles(dir);
	int index = 1;

	lua_newtable(L);

	for (char **i = rc; *i != 0; i++)
	{
		if (hascallback)
		{
			lua_pushvalue(L, 2);
			lua_pushstring(L, *i);
			lua_call(L, 1, 0);
		}

		lua_pushstring(L, *i);
		lua_rawseti(L, -2, index);
		index++;
	}

	PHYSFS_freeList(rc);

	return 1;
}

int64 Filesystem::getLastModified(const char *filename) const
{
	PHYSFS_sint64 time = PHYSFS_getLastModTime(filename);

	if (time == -1)
		throw love::Exception("Could not determine file modification date.");

	return time;
}

int64 Filesystem::getSize(const char *filename) const
{
	File file(filename);
	int64 size = file.getSize();
	return size;
}

void Filesystem::setSymlinksEnabled(bool enable)
{
	PHYSFS_permitSymbolicLinks(enable ? 1 : 0);
}

bool Filesystem::areSymlinksEnabled() const
{
	return PHYSFS_symbolicLinksPermitted() != 0;
}

bool Filesystem::isSymlink(const char *filename) const
{
	return PHYSFS_isSymbolicLink(filename) != 0;
}

std::vector<std::string> &Filesystem::getRequirePath()
{
	return requirePath;
}

void Filesystem::allowMountingForPath(const std::string &path)
{
	if (std::find(allowedMountPaths.begin(), allowedMountPaths.end(), path) == allowedMountPaths.end())
		allowedMountPaths.push_back(path);
}

} // physfs
} // filesystem
} // love
