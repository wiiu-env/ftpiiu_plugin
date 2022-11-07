/*

ftpii -- an FTP server for the Wii

Copyright (C) 2008 Joseph Jordan <joe.ftpii@psychlaw.com.au>

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
#ifndef _FTP_H_
#define _FTP_H_


// max length for string (whole line send to client must be lower than FTP_BUFFER_SIZE)
#define FTPMAXPATHLEN 256

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

int32_t  create_server(uint16_t port);

void accept_ftp_client(int32_t server);

void set_ftp_password(char *new_password);

bool process_ftp_events(int32_t server);

void cleanup_ftp();

#ifdef __cplusplus
}
#endif

#endif /* _FTP_H_ */
