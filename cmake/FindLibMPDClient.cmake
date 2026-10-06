# ympd
# (c) 2013-2014 Andrew Karpow <andy@ndyk.de>
# This project's homepage is: http://www.ympd.org

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# - Try to find LibMPDClient
# Once done, this will define
#
#   LIBMPDCLIENT_FOUND - System has LibMPDClient
#   LIBMPDCLIENT_INCLUDE_DIRS - The LibMPDClient include directories
#   LIBMPDCLIENT_LIBRARIES - The libraries needed to use LibMPDClient
#   LIBMPDCLIENT_DEFINITIONS - Compiler switches required for using LibMPDClient

find_package(PkgConfig)
pkg_check_modules(PC_LIBMPDCLIENT QUIET libmpdclient)
set(LIBMPDCLIENT_DEFINITIONS ${PC_LIBMPDCLIENT_CFLAGS_OTHER})

find_path(LIBMPDCLIENT_INCLUDE_DIR
    NAMES mpd/player.h
    HINTS ${PC_LIBMPDCLIENT_INCLUDEDIR} ${PC_LIBMPDCLIENT_INCLUDE_DIRS}
)

find_library(LIBMPDCLIENT_LIBRARY
    NAMES mpdclient
    HINTS ${PC_LIBMPDCLIENT_LIBDIR} ${PC_LIBMPDCLIENT_LIBRARY_DIRS}
)

set(LIBMPDCLIENT_LIBRARIES ${LIBMPDCLIENT_LIBRARY})
set(LIBMPDCLIENT_INCLUDE_DIRS ${LIBMPDCLIENT_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibMPDClient DEFAULT_MSG
    LIBMPDCLIENT_LIBRARY LIBMPDCLIENT_INCLUDE_DIR
)

mark_as_advanced(LIBMPDCLIENT_LIBRARY LIBMPDCLIENT_INCLUDE_DIR)

