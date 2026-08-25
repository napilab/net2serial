#
#	FACILITY: A yet another TCP to RTU gateway for MODBUS
#
#	ENVIRONMENT: Linux, MUSL
#
#	DESCRIPTION:  Cmake file is supposed to be used to build target with MUSL instead of LIBC
#		Don't foreget to do: apt-get install musl-tools musl-dev
#
#	USAGE: $ cmake -DCMAKE_TOOLCHAIN_FILE=musl-toolchain.cmake ..
#
#	AUTHORS: Ruslan R. (The BadAss Sysman) Laishev
#
#	CREATION DATE:  25-MAR-2026
#


set(CMAKE_SYSTEM_NAME Linux)				# By default
set(CMAKE_C_COMPILER   /usr/bin/musl-gcc)		# Path to compiler under Linux
set(CMAKE_EXE_LINKER_FLAGS "-static")			# Just for test!!!
