#
# libwebsockets RT-Smart platform
#
# RT-Smart provides a POSIX-like socket and file API, so this platform reuses
# the compatible Unix implementations and owns the pieces whose behavior
# differs: service polling, event-pipe handling, and random generation.
#

include_directories(. ../unix)

set(LWS_PLAT_RTSMART 1)
set(LWS_PLAT_UNIX 1)

list(APPEND SOURCES
	plat/unix/unix-caps.c
	plat/rtsmart/rtsmart-misc.c
	plat/unix/unix-init.c
)

if (LWS_WITH_FILE_OPS)
	list(APPEND SOURCES plat/unix/unix-file.c)
endif()

if (LWS_WITH_NETWORK)
	list(APPEND SOURCES
		plat/rtsmart/rtsmart-pipe.c
		plat/rtsmart/rtsmart-service.c
		plat/unix/unix-sockets.c
		plat/unix/unix-fds.c
	)

	if (LWS_WITH_SYS_ASYNC_DNS)
		list(APPEND SOURCES plat/unix/unix-resolv.c)
	endif()

	if (LWS_WITH_SDEVENT AND LWS_HAVE_SYSTEMD_H)
		list(APPEND SOURCES plat/unix/unix-systemd.c)
		list(APPEND LIB_LIST_AT_END systemd)
	endif()
endif()

if (LWS_WITH_PLUGINS_API)
	list(APPEND SOURCES plat/unix/unix-plugins.c)
endif()

if (LWS_WITH_SPAWN)
	list(APPEND SOURCES plat/unix/unix-spawn.c)
endif()

if (NOT APPLE)
	list(APPEND LIB_LIST_AT_END m)
endif()

list(APPEND LIB_LIST_AT_END ${CMAKE_DL_LIBS})

exports_to_parent_scope()
set(LWS_PLAT_RTSMART ${LWS_PLAT_RTSMART} PARENT_SCOPE)
set(LWS_PLAT_UNIX ${LWS_PLAT_UNIX} PARENT_SCOPE)
set(LIB_LIST_AT_END ${LIB_LIST_AT_END} PARENT_SCOPE)
