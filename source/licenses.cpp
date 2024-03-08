// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2023 Michael Theall
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

#include "licenses.h"

#include "imgui.h"

#ifdef __SWITCH__
#include <zstd.h>
#endif

#if !defined(NDS) && !defined(__3DS__) && !defined(__SWITCH__) && !defined(__WIIU__)
#include <GLFW/glfw3.h>
#endif

char const *const g_dearImGuiVersion   = "Dear ImGui " IMGUI_VERSION;
char const *const g_dearImGuiCopyright = "Copyright (C) 2014-2023 Omar Cornut";

char const *const g_mitLicense =
    "The MIT License (MIT)\n"
    "\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy of this software "
    "and associated documentation files (the \"Software\"), to deal in the Software without "
    "restriction, including without limitation the rights to use, copy, modify, merge, publish, "
    "distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the "
    "Software is furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in all copies or "
    "substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, "
    "INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR "
    "PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR "
    "ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, "
    "ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
    "SOFTWARE.";

#ifdef __3DS__
char const *const g_libctruVersion = "libctru";
char const *const g_citro3dVersion = "citro3d";

char const *const g_citro3dCopyright = "Copyright (C) 2014-2020 fincs";
#endif

#ifdef __SWITCH__
char const *const g_libnxVersion  = "libnx";
char const *const g_deko3dVersion = "deko3d";
char const *const g_zstdVersion   = "zStandard " ZSTD_VERSION_STRING;

char const *const g_libnxCopyright  = "Copyright (C) 2017-2021 libnx Authors";
char const *const g_deko3dCopyright = "Copyright (C) 2018-2020 fincs\n";
char const *const g_zstdCopyright =
    "Copyright (C) 2016-present, Facebook, Inc. All rights reserved.";

char const *const g_libnxLicense =
    "Permission to use, copy, modify, and/or distribute this software for any purpose with or "
    "without fee is hereby granted, provided that the above copyright notice and this permission "
    "notice appear in all copies.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO "
    "THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT "
    "SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY "
    "DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF "
    "CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE "
    "OR PERFORMANCE OF THIS SOFTWARE.";

char const *const g_bsdLicense =
    "BSD License\n"
    "\n"
    "Redistribution and use in source and binary forms, with or without modification, are "
    "permitted provided that the following conditions are met:\n"
    "\n"
    "* Redistributions of source code must retain the above copyright notice, this list of "
    "conditions and the following disclaimer.\n"
    "\n"
    "* Redistributions in binary form must reproduce the above copyright notice, this list of "
    "conditions and the following disclaimer in the documentation and/or other materials provided "
    "with the distribution.\n"
    "\n"
    "* Neither the name Facebook nor the names of its contributors may be used to endorse or "
    "promote products derived from this software without specific prior written permission.\n"
    "\n"
    "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS "
    "OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF "
    "MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE "
    "COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, "
    "EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE "
    "GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED "
    "AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING "
    "NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED "
    "OF THE POSSIBILITY OF SUCH DAMAGE.";
#endif

#if !defined(NDS)
char const *const g_zlibLicense =
    "This software is provided 'as-is', without any express or implied warranty.  In no event will "
    "the authors be held liable for any damages arising from the use of this software.\n"
    "\n"
    "Permission is granted to anyone to use this software for any purpose, including commercial "
    "applications, and to alter it and redistribute it freely, subject to the following "
    "restrictions:\n"
    "\n"
    "1. The origin of this software must not be misrepresented; you must not claim that you wrote "
    "the original software. If you use this software in a product, an acknowledgment in the "
    "product documentation would be appreciated but is not required.\n"
    "2. Altered source versions must be plainly marked as such, and must not be misrepresented as "
    "being the original software.\n"
    "3. This notice may not be removed or altered from any source distribution.";
#endif

#if !defined(NDS) && !defined(__3DS__) && !defined(__SWITCH__)
#define STR(x) #x
#define XSTR(x) STR (x)
#define GLFW_VERSION_STRING                                                                        \
	XSTR (GLFW_VERSION_MAJOR) "." XSTR (GLFW_VERSION_MINOR) "." XSTR (GLFW_VERSION_REVISION)

char const *const g_glfwVersion   = "glfw " GLFW_VERSION_STRING;
char const *const g_glfwCopyright = "Copyright (C) 2002-2006 Marcus Geelnard\n"
                                    "Copyright (C) 2006-2019 Camilla Löwy";
#endif
