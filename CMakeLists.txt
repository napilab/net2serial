#++
#	FACILITY: A yet another network 2 serial gateway
#
#	AUTHOR: StarLet Squad and Ruslan R. Laishev (AKA: BadAss sysman)
#
#	CREATION DATE: 27-NOV-2025
#
#	USAGE (see README.md for the whole installation sequence):
#		$ mkdir ./build && cd ./build
#		$ cmake ../ -DCMAKE_BUILD_TYPE=Release
#		$ make -s
#		$ sudo make install
#		$ sudo make uninstall
#
#	MODIFICATION HISTORY:
#
#	25-AUG-2026	RRL	X.00-06: The musl C library builds are supported: added the toolchain file
#				cmake/musl.cmake (the kernel headers are appended by -idirafter, so the
#				musl ones always win); the library directory reported by pkg-config is
#				given to the linker explicitly - a libconfig installed to a non-standard
#				prefix was not found at the link time.
#
#	25-AUG-2026	RRL	X.00-05ECO02: README is delivered in two languages (README.md +
#				README_RU.md), both are installed with the docs.
#
#	25-AUG-2026	RRL	X.00-05: Diagnostic options are ON for any build type (the -w of the
#				release build was hiding 35 real warnings), added the ASAN build type,
#				removed the -pg from the debug build (an option, not a definition, and
#				without the matching link flag it gives no profile anyway);
#				fixed CMAKE_HOST_SYSTEM_PROCESSOR -> CMAKE_SYSTEM_PROCESSOR (cross-building);
#				fixed the order of cmake_minimum_required()/project();
#				the undefined ${LIBCONFIG_DIR} is replaced by the pkg-config results;
#				the StarLet set is consumed as an installed find_package(StarLet) package
#				instead of the 3rdparty/ submodule;
#				added the <install> and <uninstall> targets.
#
#--

cmake_minimum_required (VERSION 3.10)
cmake_policy(SET CMP0048 NEW)

include(CMakePrintHelpers)
include(CheckSymbolExists)



project(net2serial C)
include(GNUInstallDirs)							# Must follow project(): needs the compiler info
set ( CMAKE_VERBOSE_MAKEFILE ON )
set ( CMAKE_COLOR_MAKEFILE   ON )

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCONFIG REQUIRED libconfig)

#
#   LIBCONFIG_LIBRARIES holds the bare "-lconfig" name, so the library directory reported by
#   pkg-config must be given to the linker explicitly - otherwise a libconfig installed to a
#   non-standard prefix (a musl sysroot, /opt/...) is not found at the link time.
#
link_directories(${LIBCONFIG_LIBRARY_DIRS})

find_package(StarLet REQUIRED)						# The installed UTILITY$ROUTINES package,
									# see README.md how to install it

set ( N2S_TARGET        "net2serial" )

add_definitions( -D__ARCH__NAME__="${CMAKE_SYSTEM_PROCESSOR}" )		# NOT ..._HOST_..., see cross-building
add_definitions( -D_REENTRANT=1 )
cmake_print_variables(CMAKE_CURRENT_SOURCE_DIR)


add_compile_options(							# Diagnostic is ON for any build type
	-Wall -Wextra -Wformat=2
	-Werror=format
	-Werror=return-type
	-Werror=implicit-function-declaration
	-Wno-unused-parameter
	-Wno-missing-braces						# The StarLet's $ASCINI/OPTS_NULL macro style
)

string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type)			# Additional definitons for DEBUG builds
if (build_type STREQUAL debug)
	add_definitions( -D_DEBUG=1 -D__TRACE__=1 )
	add_compile_options( -g3 -O0 -fno-omit-frame-pointer )
endif()

#
#   An additional build type with the run-time checking of memory access and UB:
#	$ cmake ../ -DCMAKE_BUILD_TYPE=Asan
#
if (build_type STREQUAL asan)
	add_definitions( -D_DEBUG=1 -D__TRACE__=1 )
	add_compile_options( -g3 -O1 -fsanitize=address,undefined -fno-omit-frame-pointer )
	add_link_options( -fsanitize=address,undefined )
endif()


check_symbol_exists(TIOCSRS485 sys/ioctl.h HAVE_TIOCRS485)
if(HAVE_TIOCRS485)
    add_definitions(-DHAVE_TIOCRS485)
endif()


include_directories (
	src/
	${LIBCONFIG_INCLUDE_DIRS}
)

set ( N2S_SOURCES						# Network to Serial gateway
	src/defs.h
	src/msgs.h
	src/net.c
	src/tty.c
	src/main.c
)


add_executable ( ${N2S_TARGET} ${N2S_SOURCES})
target_link_libraries( ${N2S_TARGET} StarLet::starlet pthread rt ${LIBCONFIG_LIBRARIES})


#------------------------------------------------------------------------------------------------------
#	Installation:	sbin/net2serial
#			etc/net2serial/*.conf	- the existing configuration files are never overwritten
#			share/doc/net2serial/	- README, BLOCKNOTE, the User Guides, the reference configs
#
install (TARGETS ${N2S_TARGET}
	RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
)

install (FILES
	README.md
	README_RU.md
	BLOCKNOTE.md
	net2serial.conf
	net2serial_settings.conf
	docs/UserGuide_EN.md
	docs/UserGuide_RU.md
	docs/UserGuide_EN.pdf
	docs/UserGuide_RU.pdf
	DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

#	The configuration files are put to etc/ only when they are absent: an update must not
#	destroy the has been tuned working configuration.
install (CODE "
	file (MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/net2serial\")
	foreach (l_conf net2serial.conf net2serial_settings.conf)
		set (l_dst \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/net2serial/\${l_conf}\")
		if (NOT EXISTS \"\${l_dst}\")
			file (INSTALL \"${CMAKE_CURRENT_SOURCE_DIR}/\${l_conf}\"
				DESTINATION \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/net2serial\")
		else ()
			message (STATUS \"Keeping the existing \${l_dst}\")
		endif ()
	endforeach ()
")

#	'make uninstall': removes everything from install_manifest.txt; the configuration files
#	which were kept (not overwritten) at the install time are not touched.
add_custom_target (uninstall
	COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake
	COMMENT "Uninstalling by install_manifest.txt"
)


add_custom_target(files_config SOURCES
	net2serial.conf
	net2serial_settings.conf

	README.md
	BLOCKNOTE.md
)
