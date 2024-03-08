// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2020 Michael Theall
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "platform.h"
#include "version.h"

#include "IOAbstraction.h"
#include "ftpServer.h"
#include "log.h"

#include <mocha/mocha.h>
#include <nn/ac.h>
#include <thread>

#include <coreinit/thread.h>
#include <whb/proc.h>
#include <wups.h>
#include <wups/config/WUPSConfigCategory.h>
#include <wups/config/WUPSConfigItem.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemStub.h>

#ifndef CLASSIC
#error "Wii U must be built in classic mode"
#endif
#define VERSION "v0.4.1"
#define VERSION_FULL VERSION VERSION_EXTRA

WUPS_PLUGIN_NAME ("ftpiiu");
WUPS_PLUGIN_DESCRIPTION ("FTP Server based on ftpd");
WUPS_PLUGIN_VERSION (VERSION_FULL);
WUPS_PLUGIN_AUTHOR ("mtheall, Maschell");
WUPS_PLUGIN_LICENSE ("GPL3");

WUPS_USE_WUT_DEVOPTAB ();
WUPS_USE_STORAGE ("ftpiiu"); // Unique id for the storage api

#define DEFAULT_FTPIIU_ENABLED_VALUE true
#define DEFAULT_SYSTEM_FILES_ALLOWED_VALUE false

#define FTPIIU_ENABLED_STRING "enabled"
#define SYSTEM_FILES_ALLOWED_STRING "systemFilesAllowed"

bool platform::networkVisible ()
{
	return true;
}

bool platform::networkAddress (SockAddr &addr_)
{
	struct sockaddr_in addr = {};
	addr.sin_family         = AF_INET;
	nn::ac::GetAssignedAddress (&addr.sin_addr.s_addr);
	addr_ = addr;
	return true;
}

MochaUtilsStatus MountWrapper (const char *mount, const char *dev, const char *mountTo)
{
	auto res = Mocha_MountFS (mount, dev, mountTo);
	if (res == MOCHA_RESULT_ALREADY_EXISTS)
	{
		res = Mocha_MountFS (mount, nullptr, mountTo);
	}
	if (res == MOCHA_RESULT_SUCCESS)
	{
		std::string mountPath = std::string (mount) + ":/";
		debug ("Mounted %s", mountPath.c_str ());
	}
	else
	{
		error ("Failed to mount %s: %s [%d]", mount, Mocha_GetStatusStr (res), res);
	}
	return res;
}

UniqueFtpServer server             = nullptr;
static bool sSystemFilesAllowed    = DEFAULT_SYSTEM_FILES_ALLOWED_VALUE;
static bool sMochaPathsWereMounted = false;
static bool sFTPServerEnabled      = DEFAULT_FTPIIU_ENABLED_VALUE;

void start_server ()
{
	if (server != nullptr)
	{
		return;
	}

	MochaUtilsStatus res;
	if ((res = Mocha_InitLibrary ()) == MOCHA_RESULT_SUCCESS)
	{
		std::vector<std::string> virtualDirsInRoot;
		if (sSystemFilesAllowed)
		{
			if (MountWrapper ("slccmpt01", "/dev/slccmpt01", "/vol/storage_slccmpt01") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("slccmpt01");
				IOAbstraction::addVirtualPath ("slccmpt01:/", {});
			}
			if (MountWrapper ("storage_odd_tickets", nullptr, "/vol/storage_odd01") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("storage_odd_tickets");
				IOAbstraction::addVirtualPath ("storage_odd_tickets:/", {});
			}
			if (MountWrapper ("storage_odd_updates", nullptr, "/vol/storage_odd02") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("storage_odd_updates");
				IOAbstraction::addVirtualPath ("storage_odd_updates:/", {});
			}
			if (MountWrapper ("storage_odd_content", nullptr, "/vol/storage_odd03") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("storage_odd_content");
				IOAbstraction::addVirtualPath ("storage_odd_content:/", {});
			}
			if (MountWrapper ("storage_odd_content2", nullptr, "/vol/storage_odd04") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("storage_odd_content2");
				IOAbstraction::addVirtualPath ("storage_odd_content2:/", {});
			}
			if (MountWrapper ("storage_slc", "/dev/slc01", "/vol/storage_slc01") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("storage_slc");
				IOAbstraction::addVirtualPath ("storage_slc:/", {});
			}
			if (Mocha_MountFS ("storage_mlc", nullptr, "/vol/storage_mlc01") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("storage_mlc");
				IOAbstraction::addVirtualPath ("storage_mlc:/", {});
			}
			if (Mocha_MountFS ("storage_usb", nullptr, "/vol/storage_usb01") ==
			    MOCHA_RESULT_SUCCESS)
			{
				virtualDirsInRoot.emplace_back ("storage_usb");
				IOAbstraction::addVirtualPath ("storage_usb:/", {});
			}
		}
		virtualDirsInRoot.emplace_back ("fs");
		IOAbstraction::addVirtualPath (":/", virtualDirsInRoot);
		IOAbstraction::addVirtualPath ("fs:/", std::vector<std::string>{"vol"});
		IOAbstraction::addVirtualPath (
		    "fs:/vol", std::vector<std::string>{"external01", "content", "save"});

		IOAbstraction::addVirtualPath ("fs:/vol/content", {});
		sMochaPathsWereMounted = true;
	}
	else
	{
		OSReport ("Failed to init libmocha: %s [%d]\n", Mocha_GetStatusStr (res), res);
	}

	server = FtpServer::create ();
}

void stop_server ()
{
	server.reset ();
	if (sMochaPathsWereMounted)
	{
		Mocha_UnmountFS ("slccmpt01");
		Mocha_UnmountFS ("storage_odd_tickets");
		Mocha_UnmountFS ("storage_odd_updates");
		Mocha_UnmountFS ("storage_odd_content");
		Mocha_UnmountFS ("storage_odd_content2");
		Mocha_UnmountFS ("storage_slc");
		Mocha_UnmountFS ("storage_mlc");
		Mocha_UnmountFS ("storage_usb");
		sMochaPathsWereMounted = false;
	}

	IOAbstraction::clear ();
}

static void gFTPServerRunningChanged (ConfigItemBoolean *item, bool newValue)
{
	sFTPServerEnabled = newValue;
	if (!sFTPServerEnabled)
	{
		stop_server ();
	}
	else
	{
		start_server ();
	}
	// If the value has changed, we store it in the storage.
	auto res = WUPSStorageAPI::Store (FTPIIU_ENABLED_STRING, sFTPServerEnabled);
	if (res != WUPS_STORAGE_ERROR_SUCCESS)
	{
		OSReport ("Failed to store gFTPServerEnabled: %s (%d)\n",
		    WUPSStorageAPI::GetStatusStr (res).data (),
		    res);
	}
}

static void gSystemFilesAllowedChanged (ConfigItemBoolean *item, bool newValue)
{
	// DEBUG_FUNCTION_LINE("New value in gFTPServerEnabled: %d", newValue);
	if (server != nullptr)
	{ // If the server is already running we need to restart it.
		stop_server ();
		sSystemFilesAllowed = newValue;
		start_server ();
	}
	else
	{
		sSystemFilesAllowed = newValue;
	}
	// If the value has changed, we store it in the storage.
	auto res = WUPSStorageAPI::Store (SYSTEM_FILES_ALLOWED_STRING, sSystemFilesAllowed);
	if (res != WUPS_STORAGE_ERROR_SUCCESS)
	{
		OSReport ("Failed to store gSystemFilesAllowed: %s (%d)\n",
		    WUPSStorageAPI::GetStatusStr (res).data (),
		    res);
	}
}

WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback (WUPSConfigCategoryHandle rootHandle)
{
	uint32_t hostIpAddress = 0;
	nn::ac::GetAssignedAddress (&hostIpAddress);
	try
	{
		WUPSConfigCategory root = WUPSConfigCategory (rootHandle);
		root.add (WUPSConfigItemBoolean::Create (FTPIIU_ENABLED_STRING,
		    "Enable ftpd",
		    true,
		    sFTPServerEnabled,
		    &gFTPServerRunningChanged));

		root.add (WUPSConfigItemBoolean::Create (SYSTEM_FILES_ALLOWED_STRING,
		    "Allow access to system files",
		    false,
		    sSystemFilesAllowed,
		    &gSystemFilesAllowedChanged));

		root.add (WUPSConfigItemStub::Create ("==="));

		char ipSettings[50];
		if (hostIpAddress != 0)
		{
			snprintf (ipSettings,
			    50,
			    "IP of your console is %u.%u.%u.%u. Port %i",
			    (hostIpAddress >> 24) & 0xFF,
			    (hostIpAddress >> 16) & 0xFF,
			    (hostIpAddress >> 8) & 0xFF,
			    (hostIpAddress >> 0) & 0xFF,
			    21);
		}
		else
		{
			snprintf (
			    ipSettings, sizeof (ipSettings), "The console is not connected to a network.");
		}

		root.add (WUPSConfigItemStub::Create (ipSettings));
		root.add (WUPSConfigItemStub::Create ("You can connect with empty credentials"));
	}
	catch (std::exception &e)
	{
		OSReport ("fptiiu plugin: Exception: %s\n", e.what ());
		return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
	}

	return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

void ConfigMenuClosedCallback ()
{
	WUPSStorageAPI::SaveStorage ();
}

INITIALIZE_PLUGIN ()
{
	WUPSConfigAPIOptionsV1 configOptions = {.name = "ftpiiu"};
	if (WUPSConfigAPI_Init (configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback) !=
	    WUPSCONFIG_API_RESULT_SUCCESS)
	{
		OSFatal ("ftpiiu plugin: Failed to init config api");
	}

	WUPSStorageError err;
	if ((err = WUPSStorageAPI::GetOrStoreDefault (
	         FTPIIU_ENABLED_STRING, sFTPServerEnabled, DEFAULT_FTPIIU_ENABLED_VALUE)) !=
	    WUPS_STORAGE_ERROR_SUCCESS)
	{
		OSReport ("ftpiiu plugin: Failed to get or create item \"%s\": %s (%d)\n",
		    FTPIIU_ENABLED_STRING,
		    WUPSStorageAPI_GetStatusStr (err),
		    err);
	}
	if ((err = WUPSStorageAPI::GetOrStoreDefault (SYSTEM_FILES_ALLOWED_STRING,
	         sSystemFilesAllowed,
	         DEFAULT_SYSTEM_FILES_ALLOWED_VALUE)) != WUPS_STORAGE_ERROR_SUCCESS)
	{
		OSReport ("ftpiiu plugin: Failed to get or create item \"%s\": %s (%d)\n",
		    SYSTEM_FILES_ALLOWED_STRING,
		    WUPSStorageAPI_GetStatusStr (err),
		    err);
	}

	if ((err = WUPSStorageAPI::SaveStorage ()) != WUPS_STORAGE_ERROR_SUCCESS)
	{
		OSReport ("ftpiiu plugin: Failed to save storage: %s (%d)\n",
		    WUPSStorageAPI_GetStatusStr (err),
		    err);
	}
}

void wiiu_init ()
{
	nn::ac::Initialize ();
	nn::ac::ConnectAsync ();
	if (sFTPServerEnabled)
	{
		start_server ();
	}
}

ON_APPLICATION_START ()
{
	nn::ac::Initialize ();
	nn::ac::ConnectAsync ();

	wiiu_init ();
}

ON_APPLICATION_ENDS ()
{
	stop_server ();
}

bool platform::init ()
{
	WHBProcInit ();
	wiiu_init ();
	return true;
}

bool platform::loop ()
{
	return WHBProcIsRunning ();
}

void platform::render ()
{
}

void platform::exit ()
{
	IOAbstraction::clear ();
	WHBProcShutdown ();
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform thread pimpl
class platform::Thread::privateData_t
{
public:
	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	explicit privateData_t (std::function<void ()> &&func_) : thread (std::move (func_))
	{
		auto nativeHandle = (OSThread *)thread.native_handle ();
		OSSetThreadName (nativeHandle, "ftpiiu");
		while (!OSSetThreadAffinity (nativeHandle, OS_THREAD_ATTRIB_AFFINITY_CPU2))
		{
			OSSleepTicks (OSMillisecondsToTicks (16));
		}
		while (!OSSetThreadPriority (nativeHandle, 16))
		{
			OSSleepTicks (OSMillisecondsToTicks (16));
		}
	}

	/// \brief Underlying thread
	std::thread thread;
};

///////////////////////////////////////////////////////////////////////////
platform::Thread::~Thread () = default;

platform::Thread::Thread () : m_d (new privateData_t ())
{
}

platform::Thread::Thread (std::function<void ()> &&func_)
    : m_d (new privateData_t (std::move (func_)))
{
}

platform::Thread::Thread (Thread &&that_) : m_d (new privateData_t ())
{
	std::swap (m_d, that_.m_d);
}

platform::Thread &platform::Thread::operator= (Thread &&that_)
{
	std::swap (m_d, that_.m_d);
	return *this;
}

void platform::Thread::join ()
{
	m_d->thread.join ();
}

void platform::Thread::sleep (std::chrono::milliseconds const timeout_)
{
	std::this_thread::sleep_for (timeout_);
}

///////////////////////////////////////////////////////////////////////////
#define USE_STD_MUTEX 1

/// \brief Platform mutex pimpl
class platform::Mutex::privateData_t
{
public:
#if USE_STD_MUTEX
	/// \brief Underlying mutex
	std::mutex mutex;
#else
	/// \brief Underlying mutex
	::Mutex mutex;
#endif
};

///////////////////////////////////////////////////////////////////////////
platform::Mutex::~Mutex () = default;

platform::Mutex::Mutex () : m_d (new privateData_t ())
{
#if !USE_STD_MUTEX
	mutexInit (&m_d->mutex);
#endif
}

void platform::Mutex::lock ()
{
#if USE_STD_MUTEX
	m_d->mutex.lock ();
#else
	mutexLock (&m_d->mutex);
#endif
}

void platform::Mutex::unlock ()
{
#if USE_STD_MUTEX
	m_d->mutex.unlock ();
#else
	mutexUnlock (&m_d->mutex);
#endif
}
