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

#include "../IOAbstraction.h"
#include "log.h"

#include <mocha/mocha.h>
#include <nn/ac.h>
#include <thread>

#include <coreinit/thread.h>
#include <sys/unistd.h>
#include <whb/proc.h>

#ifndef CLASSIC
#error "Wii U must be built in classic mode"
#endif

bool platform::networkVisible ()
{
	return true;
}

bool platform::networkAddress (SockAddr &addr_)
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
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

bool platform::init ()
{
	nn::ac::Initialize ();
	nn::ac::ConnectAsync ();
	WHBProcInit ();

	MochaUtilsStatus res;
	if ((res = Mocha_InitLibrary ()) == MOCHA_RESULT_SUCCESS)
	{
		std::vector<std::string> virtualDirsInRoot;
		if (MountWrapper ("slccmpt01", "/dev/slccmpt01", "/vol/storage_slccmpt01") ==
		    MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("slccmpt01");
		}
		if (MountWrapper ("storage_odd_tickets", nullptr, "/vol/storage_odd01") ==
		    MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("storage_odd_tickets");
		}
		if (MountWrapper ("storage_odd_updates", nullptr, "/vol/storage_odd02") ==
		    MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("storage_odd_updates");
		}
		if (MountWrapper ("storage_odd_content", nullptr, "/vol/storage_odd03") ==
		    MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("storage_odd_content");
		}
		if (MountWrapper ("storage_odd_content2", nullptr, "/vol/storage_odd04") ==
		    MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("storage_odd_content2");
		}
		if (MountWrapper ("storage_slc", "/dev/slc01", "/vol/storage_slc01") ==
		    MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("storage_slc");
		}
		if (Mocha_MountFS ("storage_mlc", nullptr, "/vol/storage_mlc01") == MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("storage_mlc");
		}
		if (Mocha_MountFS ("storage_usb", nullptr, "/vol/storage_usb01") == MOCHA_RESULT_SUCCESS)
		{
			virtualDirsInRoot.push_back ("storage_usb");
		}
		virtualDirsInRoot.push_back ("fs");
		IOAbstraction::addVirtualPath (":/", virtualDirsInRoot);
		IOAbstraction::addVirtualPath ("fs:/", std::vector<std::string>{"vol"});
		IOAbstraction::addVirtualPath ("fs:/vol", std::vector<std::string>{"external01"});
		IOAbstraction::addVirtualPath ("storage_odd_tickets:/", {});
		IOAbstraction::addVirtualPath ("storage_odd_updates:/", {});
		IOAbstraction::addVirtualPath ("storage_odd_content:/", {});
		IOAbstraction::addVirtualPath ("storage_odd_content2:/", {});
		IOAbstraction::addVirtualPath ("storage_usb:/", {});
	}
	else
	{
		error ("Failed to init libmocha: %s [%d]", Mocha_GetStatusStr (res), res);
	}

	::chdir ("fs:/vol/external01");

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
		OSSetThreadName (nativeHandle, "ftpd_server");
		while (!OSSetThreadAffinity (nativeHandle, OS_THREAD_ATTRIB_AFFINITY_CPU2))
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
