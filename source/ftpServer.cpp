// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2024 Michael Theall
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

#include "ftpServer.h"

#include "fs.h"
#include "ftpConfig.h"
#include "ftpSession.h"
#include "log.h"
#include "platform.h"
#include "sockAddr.h"
#include "socket.h"

#ifndef __WIIU__
#include "imgui.h"
#include "licenses.h"
#endif
#ifndef __NDS__
#include "mdns.h"
#endif

#ifdef __NDS__
#include <dswifi9.h>
#endif

#ifdef __3DS__
#include <citro3d.h>
#endif

#ifndef CLASSIC
#include <imgui.h>

#include <jansson.h>

#include <curl/easy.h>
#include <curl/multi.h>
#ifndef NDEBUG
#include <curl/curl.h>
#endif
#endif

#include <sys/statvfs.h>
using statvfs_t = struct statvfs;

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
using namespace std::chrono_literals;

#ifdef __NDS__
#define LOCKED(x) x
#else
#define LOCKED(x)                                                                                  \
	do                                                                                             \
	{                                                                                              \
		auto const lock = std::scoped_lock (m_lock);                                               \
		x;                                                                                         \
	} while (0)
#endif

namespace
{
/// \brief Application start time
auto const s_startTime = std::time (nullptr);

#ifdef __3DS__
/// \brief Timezone offset in seconds (only used on 3DS)
int s_tzOffset = 0;
#endif

#ifndef __NDS__
/// \brief Mutex for s_freeSpace
platform::Mutex s_lock;
#endif

/// \brief Free space string
std::string s_freeSpace;

#ifndef CLASSIC
#ifndef NDEBUG
std::string printable (std::string_view const data_)
{
	unsigned count = 0;
	for (auto const &c : data_)
	{
		if (c != '%' && (std::isprint (c) || std::isspace (c)))
			++count;
		else
			count += 3;
	}

	std::string result;
	result.reserve (count);

	for (auto const &c : data_)
	{
		if (c != '%' && (std::isprint (c) || std::isspace (c)))
			result.push_back (c);
		else
		{
			result.push_back ('%');

			auto const upper = (static_cast<unsigned char> (c) >> 4u) & 0xF;
			auto const lower = (static_cast<unsigned char> (c) >> 0u) & 0xF;

			result.push_back (gsl::narrow_cast<char> (upper < 10 ? upper + '0' : upper + 'A' - 10));
			result.push_back (gsl::narrow_cast<char> (lower < 10 ? lower + '0' : lower + 'A' - 10));
		}
	}

	return result;
}

int curlDebug (CURL *const handle_,
    curl_infotype const type_,
    char *const data_,
    std::size_t const size_,
    void *const user_)
{
	(void)handle_;
	(void)user_;

	auto const text = printable (std::string_view (data_, size_));

	switch (type_)
	{
	case CURLINFO_TEXT:
		info ("== Info: %s", text.c_str ());
		break;

	case CURLINFO_HEADER_OUT:
		info ("=> Send header: %s", text.c_str ());
		break;

	case CURLINFO_DATA_OUT:
		info ("=> Send data: %s", text.c_str ());
		break;

	case CURLINFO_SSL_DATA_OUT:
		info ("=> Send SSL data: %s", text.c_str ());
		break;

	case CURLINFO_HEADER_IN:
		info ("<= Receive header: %s", text.c_str ());
		break;

	case CURLINFO_DATA_IN:
		info ("<= Receive data: %s", text.c_str ());
		break;

	case CURLINFO_SSL_DATA_IN:
		info ("<= Receive SSL data: %s", text.c_str ());
		break;

	default:
		break;
	}

	return 0;
}
#endif

std::size_t curlCallback (void *const contents_,
    std::size_t const size_,
    std::size_t const count_,
    void *const user_)
{
	auto const total = size_ * count_;
	auto const start = static_cast<char *> (contents_);
	auto const end   = start + total;

	auto &result = *static_cast<std::string *> (user_);
	result.insert (std::end (result), start, end);

	return total;
}
#endif
}

///////////////////////////////////////////////////////////////////////////
FtpServer::~FtpServer ()
{
	m_quit = true;

#ifndef __NDS__
	m_thread.join ();
#endif

#ifndef CLASSIC
	if (m_uploadLogCurl)
	{
		curl_multi_remove_handle (m_uploadLogCurlM, m_uploadLogCurl);
		curl_easy_cleanup (m_uploadLogCurl);
		curl_mime_free (m_uploadLogMime);
	}

	if (m_uploadLogCurlM)
		curl_multi_cleanup (m_uploadLogCurlM);
#endif
}

FtpServer::FtpServer (UniqueFtpConfig config_)
    : m_config (std::move (config_))
#ifndef CLASSIC
      ,
      m_hostnameSetting (m_config->hostname ())
#endif
{
#ifndef __NDS__
	mdns::setHostname (m_config->hostname ());

	m_thread = platform::Thread (std::bind (&FtpServer::threadFunc, this));
#endif

#ifdef __3DS__
	s64 tzOffsetMinutes;
	if (R_SUCCEEDED (svcGetSystemInfo (&tzOffsetMinutes, 0x10000, 0x103)))
		s_tzOffset = tzOffsetMinutes * 60;
#endif
}

void FtpServer::draw ()
{
#ifdef __NDS__
	loop ();
#endif

#ifdef CLASSIC
	{
		char port[7];
#ifndef __NDS__
		auto const lock = std::scoped_lock (m_lock);
#endif
		if (m_socket)
			std::sprintf (port, ":%u", m_socket->sockName ().port ());

#ifndef NO_CONSOLE
		consoleSelect (&g_statusConsole);
		std::printf ("\x1b[0;0H\x1b[32;1m%s \x1b[36;1m%s%s",
		    STATUS_STRING,
		    m_socket ? m_socket->sockName ().name () : "Waiting on WiFi",
		    m_socket ? port : "");
#endif

#ifndef __NDS__
		char timeBuffer[16];
		auto const now = std::time (nullptr);
		std::strftime (timeBuffer, sizeof (timeBuffer), "%H:%M:%S", std::localtime (&now));

		std::printf (" \x1b[37;1m%s", timeBuffer);
#endif

		std::fputs ("\x1b[K", stdout);
		std::fflush (stdout);
	}

	{
#ifndef __NDS__
		auto const lock = std::scoped_lock (s_lock);
#endif
		if (!s_freeSpace.empty ())
		{
#ifndef NO_CONSOLE
			consoleSelect (&g_statusConsole);
			std::printf ("\x1b[0;%uH\x1b[32;1m%s",
			    static_cast<unsigned> (g_statusConsole.windowWidth - s_freeSpace.size () + 1),
			    s_freeSpace.c_str ());
			std::fflush (stdout);

#endif
		}
	}

	{
#ifndef __NDS__
		auto const lock = std::scoped_lock (m_lock);
#endif
#ifndef NO_CONSOLE
		consoleSelect (&g_sessionConsole);
		std::fputs ("\x1b[2J", stdout);
		for (auto &session : m_sessions)
		{
			session->draw ();
			if (&session != &m_sessions.back ())
				std::fputc ('\n', stdout);
		}
		std::fflush (stdout);
#endif
	}

	drawLog ();
#else
	auto const &io    = ImGui::GetIO ();
	auto const width  = io.DisplaySize.x;
	auto const height = io.DisplaySize.y;

	ImGui::SetNextWindowPos (ImVec2 (0, 0), ImGuiCond_FirstUseEver);
#ifdef __3DS__
	// top screen
	ImGui::SetNextWindowSize (ImVec2 (width, height * 0.5f));
#else
	ImGui::SetNextWindowSize (ImVec2 (width, height));
#endif
	{
		std::array<char, 64> title{};

		{
			auto const serverLock = std::scoped_lock (m_lock);
			std::snprintf (title.data (),
			    title.size (),
			    STATUS_STRING " %s###ftpd",
			    m_socket ? m_name.c_str () : "Waiting for WiFi...");
		}

		ImGui::Begin (title.data (),
		    nullptr,
		    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
#ifndef __3DS__
		        | ImGuiWindowFlags_MenuBar
#endif
		);
	}

#ifndef __3DS__
	showMenu ();
#endif

#ifndef __3DS__
	ImGui::BeginChild (
	    "Logs", ImVec2 (0, 0.5f * height), false, ImGuiWindowFlags_HorizontalScrollbar);
#endif
	drawLog ();
#ifndef __3DS__
	ImGui::EndChild ();
#endif

#ifdef __3DS__
	ImGui::End ();

	// bottom screen
	ImGui::SetNextWindowSize (ImVec2 (width * 0.8f, height * 0.5f));
	ImGui::SetNextWindowPos (ImVec2 (width * 0.1f, height * 0.5f), ImGuiCond_FirstUseEver);
	ImGui::Begin ("Sessions",
	    nullptr,
	    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
	        ImGuiWindowFlags_MenuBar);

	showMenu ();
#else
	ImGui::Separator ();
#endif

	{
		auto const lock = std::scoped_lock (m_lock);
		for (auto &session : m_sessions)
			session->draw ();
	}

	ImGui::End ();
#endif
}

bool FtpServer::quit ()
{
	return m_quit;
}

UniqueFtpServer FtpServer::create ()
{
	updateFreeSpace ();

	auto config = FtpConfig::load (FTPDCONFIG);

	return UniqueFtpServer (new FtpServer (std::move (config)));
}

std::string FtpServer::getFreeSpace ()
{
#ifndef __NDS__
	auto const lock = std::scoped_lock (s_lock);
#endif
	return s_freeSpace;
}

void FtpServer::updateFreeSpace ()
{
	statvfs_t st = {};
#if defined(__NDS__) || defined(__3DS__) || defined(__SWITCH__)
	if (::statvfs ("sdmc:/", &st) != 0)
#else
	if (::statvfs ("/", &st) != 0)
#endif
		return;

	auto freeSpace = fs::printSize (static_cast<std::uint64_t> (st.f_bsize) * st.f_bfree);

#ifndef __NDS__
	auto const lock = std::scoped_lock (s_lock);
#endif
	if (freeSpace != s_freeSpace)
		s_freeSpace = std::move (freeSpace);
}

std::time_t FtpServer::startTime ()
{
	return s_startTime;
}

#ifdef __3DS__
int FtpServer::tzOffset ()
{
	return s_tzOffset;
}
#endif

void FtpServer::handleNetworkFound ()
{
	SockAddr addr;
	if (!platform::networkAddress (addr))
		return;

	std::uint16_t port;

	{
#ifndef __NDS__
		auto const lock = m_config->lockGuard ();
#endif
		port = m_config->port ();
	}

	addr.setPort (port);

	auto socket = Socket::create (Socket::eStream);
	if (!socket)
		return;

	if (port != 0 && !socket->setReuseAddress (true))
		return;

	if (!socket->bind (addr))
		return;

	if (!socket->listen (10))
		return;

	auto const &sockName = socket->sockName ();
	auto const name      = sockName.name ();

	m_name.resize (std::strlen (name) + 3 + 5);
	m_name.resize (std::sprintf (m_name.data (), "[%s]:%u", name, sockName.port ()));

	info ("Started server at %s\n", m_name.c_str ());

	LOCKED (m_socket = std::move (socket));

#ifndef __NDS__
	socket = mdns::createSocket ();
	if (!socket)
		return;

	LOCKED (m_mdnsSocket = std::move (socket));
#endif
}

void FtpServer::handleNetworkLost ()
{
	{
		// destroy sessions
		std::vector<UniqueFtpSession> sessions;
		LOCKED (sessions = std::move (m_sessions));
	}

	{
		UniqueSocket sock;

		// destroy command socket
		LOCKED (sock = std::move (m_socket));

#ifndef __NDS__
		// destroy mDNS socket
		LOCKED (sock = std::move (m_mdnsSocket));
#endif
	}

	info ("Stopped server at %s\n", m_name.c_str ());
}

#ifndef CLASSIC
void FtpServer::showMenu ()
{
	auto const prevShowSettings = m_showSettings;
	auto const prevShowAbout    = m_showAbout;

	if (ImGui::BeginMenuBar ())
	{
#if defined(__3DS__) || defined(__SWITCH__)
		if (ImGui::BeginMenu ("Menu \xee\x80\x83")) // Y Button
#else
		if (ImGui::BeginMenu ("Menu"))
#endif
		{
			if (ImGui::MenuItem ("Settings"))
				m_showSettings = true;

			if (ImGui::MenuItem ("Upload Log"))
			{
#ifndef __NDS__
				auto const lock = std::scoped_lock (m_lock);
#endif
				if (!m_uploadLogCurlM)
					m_uploadLogCurlM = curl_multi_init ();

				if (m_uploadLogCurlM && !m_uploadLogCurl.load (std::memory_order_relaxed))
				{
					m_uploadLogData = getLog ();

					auto const handle = curl_easy_init ();

#ifndef NDEBUG
					curl_easy_setopt (handle, CURLOPT_DEBUGFUNCTION, &curlDebug);
					curl_easy_setopt (handle, CURLOPT_DEBUGDATA, nullptr);
					curl_easy_setopt (handle, CURLOPT_VERBOSE, 1L);
#endif

					// write result into string
					m_uploadLogResult.clear ();
					curl_easy_setopt (handle, CURLOPT_WRITEFUNCTION, &curlCallback);
					curl_easy_setopt (handle, CURLOPT_WRITEDATA, &m_uploadLogResult);

					// set headers
					static char contentType[]       = "Content-Type: text/plain";
					static curl_slist const headers = {contentType, nullptr};
					curl_easy_setopt (handle, CURLOPT_URL, "https://pastie.io/documents");
					curl_easy_setopt (handle, CURLOPT_HTTPHEADER, &headers);

					// set form data
					auto const mime = curl_mime_init (handle);
					auto const part = curl_mime_addpart (mime);
					curl_mime_name (part, "data");
					curl_mime_data (part, m_uploadLogData.data (), m_uploadLogData.size ());
					curl_easy_setopt (handle, CURLOPT_MIMEPOST, mime);

					// add to multi handle
					curl_multi_add_handle (m_uploadLogCurlM, handle);

					// signal network thread to process
					m_uploadLogMime = mime;
					m_uploadLogCurl.store (handle, std::memory_order_relaxed);
				}
			}

			ImGui::Separator ();

			if (ImGui::MenuItem ("About"))
				m_showAbout = true;

			ImGui::Separator ();

			if (ImGui::MenuItem ("Quit"))
				m_quit = true;

			ImGui::EndMenu ();
		}
		ImGui::EndMenuBar ();
	}

	if (m_showSettings)
	{
		if (!prevShowSettings)
		{
#ifndef __NDS__
			auto const lock = m_config->lockGuard ();
#endif

			m_userSetting = m_config->user ();
			m_userSetting.resize (32);

			m_passSetting = m_config->pass ();
			m_passSetting.resize (32);

			m_hostnameSetting = m_config->hostname ();
			m_hostnameSetting.resize (32);

			m_portSetting = m_config->port ();

#ifdef __3DS__
			m_getMTimeSetting = m_config->getMTime ();
#endif

#ifdef __SWITCH__
			m_enableAPSetting = m_config->enableAP ();

			m_ssidSetting = m_config->ssid ();
			m_ssidSetting.resize (19);

			m_passphraseSetting = m_config->passphrase ();
			m_passphraseSetting.resize (63);
#endif

			ImGui::OpenPopup ("Settings");
		}

		showSettings ();
	}

	if (m_showAbout)
	{
		if (!prevShowAbout)
			ImGui::OpenPopup ("About");

		showAbout ();
	}
}

void FtpServer::showSettings ()
{
#ifdef __3DS__
	auto const &io    = ImGui::GetIO ();
	auto const width  = io.DisplaySize.x;
	auto const height = io.DisplaySize.y;

	ImGui::SetNextWindowSize (ImVec2 (width * 0.8f, height * 0.5f));
	ImGui::SetNextWindowPos (ImVec2 (width * 0.1f, height * 0.5f));
	if (ImGui::BeginPopupModal ("Settings",
	        nullptr,
	        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
#else
	if (ImGui::BeginPopupModal ("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
#endif
	{
		ImGui::InputText ("User",
		    m_userSetting.data (),
		    m_userSetting.size (),
		    ImGuiInputTextFlags_AutoSelectAll);

		ImGui::InputText ("Pass",
		    m_passSetting.data (),
		    m_passSetting.size (),
		    ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_Password);

		ImGui::InputText ("Hostname",
		    m_hostnameSetting.data (),
		    m_hostnameSetting.size (),
		    ImGuiInputTextFlags_AutoSelectAll);

		ImGui::InputScalar ("Port",
		    ImGuiDataType_U16,
		    &m_portSetting,
		    nullptr,
		    nullptr,
		    "%u",
		    ImGuiInputTextFlags_AutoSelectAll);

#ifdef __3DS__
		ImGui::Checkbox ("Get mtime", &m_getMTimeSetting);
#endif

#ifdef __SWITCH__
		ImGui::Checkbox ("Enable Access Point", &m_enableAPSetting);

		ImGui::InputText ("SSID",
		    m_ssidSetting.data (),
		    m_ssidSetting.size (),
		    ImGuiInputTextFlags_AutoSelectAll);
		auto const ssidError = platform::validateSSID (m_ssidSetting);
		if (ssidError)
			ImGui::TextColored (ImVec4 (1.0f, 0.4f, 0.4f, 1.0f), ssidError);

		ImGui::InputText ("Passphrase",
		    m_passphraseSetting.data (),
		    m_passphraseSetting.size (),
		    ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_Password);
		auto const passphraseError = platform::validatePassphrase (m_passphraseSetting);
		if (passphraseError)
			ImGui::TextColored (ImVec4 (1.0f, 0.4f, 0.4f, 1.0f), passphraseError);
#endif

		static ImVec2 const sizes[] = {
		    ImGui::CalcTextSize ("Apply"),
		    ImGui::CalcTextSize ("Save"),
		    ImGui::CalcTextSize ("Reset"),
		    ImGui::CalcTextSize ("Cancel"),
		};

		static auto const maxWidth = std::max_element (
		    std::begin (sizes), std::end (sizes), [] (auto const &lhs_, auto const &rhs_) {
			    return lhs_.x < rhs_.x;
		    })->x;

		static auto const maxHeight = std::max_element (
		    std::begin (sizes), std::end (sizes), [] (auto const &lhs_, auto const &rhs_) {
			    return lhs_.y < rhs_.y;
		    })->y;

		auto const &style = ImGui::GetStyle ();
		auto const width  = maxWidth + 2 * style.FramePadding.x;
		auto const height = maxHeight + 2 * style.FramePadding.y;

		auto const apply = ImGui::Button ("Apply", ImVec2 (width, height));
		ImGui::SameLine ();
		auto const save = ImGui::Button ("Save", ImVec2 (width, height));
		ImGui::SameLine ();
		auto const reset = ImGui::Button ("Reset", ImVec2 (width, height));
		ImGui::SameLine ();
		auto const cancel = ImGui::Button ("Cancel", ImVec2 (width, height));

		if (apply || save)
		{
			m_showSettings = false;
			ImGui::CloseCurrentPopup ();

#ifndef __NDS__
			auto const lock = m_config->lockGuard ();
#endif

			m_config->setUser (m_userSetting);
			m_config->setPass (m_passSetting);
			m_config->setHostname (m_hostnameSetting);
			m_config->setPort (m_portSetting);

#ifdef __3DS__
			m_config->setGetMTime (m_getMTimeSetting);
#endif

#ifdef __SWITCH__
			m_config->setEnableAP (m_enableAPSetting);
			m_config->setSSID (m_ssidSetting);
			m_config->setPassphrase (m_passphraseSetting);
			m_apError = false;
#endif

			UniqueSocket socket;
			LOCKED (socket = std::move (m_socket));

			mdns::setHostname (m_hostnameSetting);
		}

		if (save)
		{
#ifndef __NDS__
			auto const lock = m_config->lockGuard ();
#endif
			if (!m_config->save (FTPDCONFIG))
				error ("Failed to save config\n");
		}

		if (reset)
		{
			static auto const defaults = FtpConfig::create ();

			m_userSetting     = defaults->user ();
			m_passSetting     = defaults->pass ();
			m_hostnameSetting = defaults->hostname ();
			m_portSetting     = defaults->port ();
#ifdef __3DS__
			m_getMTimeSetting = defaults->getMTime ();
#endif

#ifdef __SWITCH__
			m_enableAPSetting   = defaults->enableAP ();
			m_ssidSetting       = defaults->ssid ();
			m_passphraseSetting = defaults->passphrase ();
#endif
		}

		if (apply || save || cancel)
		{
			m_showSettings = false;
			ImGui::CloseCurrentPopup ();
		}

		ImGui::EndPopup ();
	}
}

void FtpServer::showAbout ()
{
	auto const &io    = ImGui::GetIO ();
	auto const width  = io.DisplaySize.x;
	auto const height = io.DisplaySize.y;

#ifdef __3DS__
	ImGui::SetNextWindowSize (ImVec2 (width * 0.8f, height * 0.5f));
	ImGui::SetNextWindowPos (ImVec2 (width * 0.1f, height * 0.5f));
#else
	ImGui::SetNextWindowSize (ImVec2 (width * 0.8f, height * 0.8f));
	ImGui::SetNextWindowPos (ImVec2 (width * 0.1f, height * 0.1f));
#endif
	if (ImGui::BeginPopupModal ("About",
	        nullptr,
	        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
	{
		ImGui::TextUnformatted (STATUS_STRING);
		ImGui::TextWrapped ("Copyright © 2024 Michael Theall, Dave Murphy, TuxSH");
		ImGui::Separator ();
		ImGui::Text ("Platform: %s", io.BackendPlatformName);
		ImGui::Text ("Renderer: %s", io.BackendRendererName);

#ifdef __3DS__
		ImGui::Text ("Command Buffer Usage: %.1f%%", 100.0f * C3D_GetCmdBufUsage ());
		ImGui::Text ("GPU Processing Usage: %.1f%%", 6.0f * C3D_GetProcessingTime ());
		ImGui::Text ("GPU Drawing Usage: %.1f%%", 6.0f * C3D_GetDrawingTime ());
#endif

		if (ImGui::Button ("OK", ImVec2 (100, 0)))
		{
			m_showAbout = false;
			ImGui::CloseCurrentPopup ();
		}

		ImGui::Separator ();
		if (ImGui::TreeNode ("Connections"))
		{
			for (auto const &session : m_sessions)
				session->drawConnections ();
			ImGui::TreePop ();
		}

		ImGui::Separator ();
		if (ImGui::TreeNode (g_dearImGuiVersion))
		{
			ImGui::TextWrapped ("%s", g_dearImGuiCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_mitLicense);
			ImGui::TreePop ();
		}

#if defined(__NDS__)
#elif defined(__3DS__)
		if (ImGui::TreeNode (g_libctruVersion))
		{
			ImGui::TextWrapped ("%s", g_zlibLicense);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_zlibLicense);
			ImGui::TreePop ();
		}

		if (ImGui::TreeNode (g_citro3dVersion))
		{
			ImGui::TextWrapped ("%s", g_citro3dCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_zlibLicense);
			ImGui::TreePop ();
		}

#elif defined(__SWITCH__)
		if (ImGui::TreeNode (g_libnxVersion))
		{
			ImGui::TextWrapped ("%s", g_libnxCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_libnxLicense);
			ImGui::TreePop ();
		}

		if (ImGui::TreeNode (g_deko3dVersion))
		{
			ImGui::TextWrapped ("%s", g_deko3dCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_zlibLicense);
			ImGui::TreePop ();
		}

		if (ImGui::TreeNode (g_zstdVersion))
		{
			ImGui::TextWrapped ("%s", g_zstdCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_zstdLicense);
			ImGui::TreePop ();
		}
#else
		if (ImGui::TreeNode (g_glfwVersion))
		{
			ImGui::TextWrapped ("%s", g_glfwCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_zlibLicense);
			ImGui::TreePop ();
		}
#endif

#if defined(__NDS__) || defined(__3DS__) || defined(__SWITCH__)
		if (ImGui::TreeNode (g_globVersion))
		{
			ImGui::TextWrapped ("%s", g_globCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_globLicense);
			ImGui::TreePop ();
		}

		if (ImGui::TreeNode (g_collateVersion))
		{
			ImGui::TextWrapped ("%s", g_collateCopyright);
			ImGui::Separator ();
			ImGui::TextWrapped ("%s", g_collateLicense);
			ImGui::TreePop ();
		}
#endif

		ImGui::EndPopup ();
	}
}
#endif

void FtpServer::loop ()
{
	if (!m_socket)
	{
#ifndef CLASSIC
#ifdef __SWITCH__
		if (!m_apError)
		{
			bool enable;
			std::string ssid;
			std::string passphrase;

			{
				auto const lock = m_config->lockGuard ();
				enable          = m_config->enableAP ();
				ssid            = m_config->ssid ();
				passphrase      = m_config->passphrase ();
			}

			m_apError = !platform::enableAP (enable, ssid, passphrase);
		}
#endif
#endif

		if (platform::networkVisible ())
			handleNetworkFound ();
	}

#ifndef CLASSIC
	{
		auto const lock = std::scoped_lock (m_lock);
		if (m_uploadLogCurl.load (std::memory_order_relaxed))
		{
			int busy      = 0;
			auto const mc = curl_multi_perform (m_uploadLogCurlM, &busy);
			if (mc != CURLM_OK)
			{
				error ("curl_multi_perform: %d %s\n", mc, curl_multi_strerror (mc));

				curl_multi_remove_handle (m_uploadLogCurlM, m_uploadLogCurl);
				curl_easy_cleanup (m_uploadLogCurl);
				curl_mime_free (m_uploadLogMime);
				m_uploadLogMime = nullptr;
				m_uploadLogCurl.store (nullptr, std::memory_order_relaxed);
			}
			else
			{
				int count      = 0;
				auto const msg = curl_multi_info_read (m_uploadLogCurlM, &count);
				if (msg && msg->msg == CURLMSG_DONE && msg->easy_handle == m_uploadLogCurl)
				{
					if (msg->data.result != CURLE_OK)
						info ("cURL finished with status %d\n", msg->data.result);

					json_error_t err;
					auto const root = json_loads (m_uploadLogResult.c_str (), 0, &err);
					if (json_is_object (root))
					{
						auto const key = json_object_get (root, "key");
						if (json_is_string (key))
							info (
							    "Log uploaded to https://pastie.io/%s\n", json_string_value (key));
					}
					else
						error ("Failed to upload log\n");

					json_decref (root);

					curl_multi_remove_handle (m_uploadLogCurlM, m_uploadLogCurl);
					curl_easy_cleanup (m_uploadLogCurl);
					curl_mime_free (m_uploadLogMime);
					m_uploadLogMime = nullptr;
					m_uploadLogCurl.store (nullptr, std::memory_order_relaxed);
				}
			}
		}
	}
#endif

	// poll listen socket
	if (m_socket)
	{
		Socket::PollInfo info{*m_socket, POLLIN, 0};
		auto const rc = Socket::poll (&info, 1, 0ms);
		if (rc < 0)
		{
			handleNetworkLost ();
			return;
		}

		if (rc > 0 && (info.revents & POLLIN))
		{
			auto socket = m_socket->accept ();
			if (socket)
			{
				auto session = FtpSession::create (*m_config, std::move (socket));
				LOCKED (m_sessions.emplace_back (std::move (session)));
			}
			else
			{
				handleNetworkLost ();
				return;
			}
		}
	}

#ifndef __NDS__
	// poll mDNS socket
	if (m_socket && m_mdnsSocket)
		mdns::handleSocket (m_mdnsSocket.get (), m_socket->sockName ());
#endif

	{
		std::vector<UniqueFtpSession> deadSessions;
		{
			// remove dead sessions
#ifndef __NDS__
			auto const lock = std::scoped_lock (m_lock);
#endif
			auto it = std::begin (m_sessions);
			while (it != std::end (m_sessions))
			{
				auto &session = *it;
				if (session->dead ())
				{
					deadSessions.emplace_back (std::move (session));
					it = m_sessions.erase (it);
				}
				else
					++it;
			}
		}
	}

	// poll sessions
	if (!m_sessions.empty ())
	{
		if (!FtpSession::poll (m_sessions))
			handleNetworkLost ();
	}
#ifndef __NDS__
	// avoid busy polling in background thread
	else
		platform::Thread::sleep (16ms);
#endif
}

void FtpServer::threadFunc ()
{
	while (!m_quit)
		loop ();
}
