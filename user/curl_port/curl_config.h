#ifndef NOTOS_CURL_CONFIG_H
#define NOTOS_CURL_CONFIG_H

#define CURL_STATICLIB 1
#define HTTP_ONLY 1

/* Protocols disabled */
#define CURL_DISABLE_FTP 1
#define CURL_DISABLE_FILE 1
#define CURL_DISABLE_LDAP 1
#define CURL_DISABLE_LDAPS 1
#define CURL_DISABLE_RTSP 1
#define CURL_DISABLE_PROXY 1
#define CURL_DISABLE_DICT 1
#define CURL_DISABLE_TELNET 1
#define CURL_DISABLE_TFTP 1
#define CURL_DISABLE_POP3 1
#define CURL_DISABLE_IMAP 1
#define CURL_DISABLE_SMB 1
#define CURL_DISABLE_SMTP 1
#define CURL_DISABLE_GOPHER 1
#define CURL_DISABLE_MQTT 1
#define CURL_DISABLE_WEBSOCKETS 1
#define CURL_DISABLE_IPFS 1
#define CURL_DISABLE_ALTSVC 1
#define CURL_DISABLE_HSTS 1
#define CURL_DISABLE_DOH 1
#define CURL_DISABLE_COOKIES 1
#define CURL_DISABLE_SHUFFLE_DNS 1
#define CURL_DISABLE_SOCKETPAIR 1
#define CURL_DISABLE_NETRC 1
#define CURL_DISABLE_MIME 1
#define CURL_DISABLE_FORM_API 1
#define CURL_DISABLE_HEADERS_API 1
#define CURL_DISABLE_LIBCURL_OPTION 1
#define CURL_DISABLE_BINDLOCAL 1
#define CURL_DISABLE_OPENSSL 1

/* OS and package metadata */
#define CURL_OS "notos"
#define OS "i486-notos"
#define PACKAGE "curl"
#define PACKAGE_NAME "curl"
#define PACKAGE_BUGREPORT "https://github.com/enderliker/NotOS"
#define PACKAGE_STRING "curl 8.11.1"
#define PACKAGE_TARNAME "curl"
#define PACKAGE_VERSION "8.11.1"
#define VERSION "8.11.1"

#define STDC_HEADERS 1

/* Sizes */
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_OFF_T 8
#define SIZEOF_CURL_OFF_T 8
#define SIZEOF_SIZE_T 4
#define SIZEOF_TIME_T 8

#define HAVE_LONGLONG 1
#define CURL_FORMAT_CURL_OFF_T "lld"
#define CURL_FORMAT_CURL_OFF_TU "llu"

/* Use poll directly; select is a separate libc adapter. */
#define HAVE_POLL 1
#define HAVE_POLL_H 1
#define HAVE_POLL_FINE 1
#undef HAVE_SELECT
#undef HAVE_SYS_SELECT_H

/* Network and sockets */
#define HAVE_SYS_SOCKET_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_NETDB_H 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETADDRINFO_THREADSAFE 1
#define HAVE_FREEADDRINFO 1
#define HAVE_SOCKET 1
#define HAVE_CONNECT 1
#define HAVE_SEND 1
#define HAVE_RECV 1
#define HAVE_SHUTDOWN 1
#define HAVE_GETSOCKOPT 1
#define HAVE_SETSOCKOPT 1
#define HAVE_GETSOCKNAME 1
#define HAVE_GETPEERNAME 1
#define HAVE_INET_NTOP 1
#define HAVE_INET_PTON 1
#define HAVE_STRUCT_TIMEVAL 1
#define HAVE_MSG_NOSIGNAL 1
#define HAVE_FCNTL 1
#define HAVE_FCNTL_O_NONBLOCK 1
#define HAVE_IOCTL_FIONBIO 1

#define SEND_TYPE_ARG1 int
#define SEND_QUAL_ARG2 const
#define SEND_TYPE_ARG2 void *
#define SEND_TYPE_ARG3 size_t
#define SEND_TYPE_ARG4 int
#define SEND_TYPE_RETV ssize_t

#define RECV_TYPE_ARG1 int
#define RECV_TYPE_ARG2 void *
#define RECV_TYPE_ARG3 size_t
#define RECV_TYPE_ARG4 int
#define RECV_TYPE_RETV ssize_t

/* System headers and functions */
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_TIME_H 1
#define HAVE_ERRNO_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_LOCALE_H 1
#define HAVE_SETLOCALE 1

#define HAVE_GETTIMEOFDAY 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_GMTIME_R 1
#define HAVE_SNPRINTF 1
#define HAVE_STRDUP 1
#define HAVE_STRTOLL 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_FTRUNCATE 1
#define HAVE_ISATTY 1
#define HAVE_BASENAME 1
#define HAVE_GETPID 1

/* Facilities explicitly NOT supported */
#undef HAVE_FORK
#undef HAVE_SIGACTION
#undef HAVE_SIGNAL
#undef HAVE_ALARM
#undef HAVE_SYS_IOCTL_H
#undef HAVE_NET_IF_H
#undef HAVE_SYS_PARAM_H
#undef HAVE_SYS_RESOURCE_H
#undef HAVE_SYS_WAIT_H
#undef HAVE_TERMIOS_H
#undef HAVE_PWD_H
#undef HAVE_GRP_H
#undef USE_UNIX_SOCKETS
#undef USE_THREADS_POSIX
#undef USE_ARES

#endif
