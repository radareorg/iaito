# - Find Radare2 (libr)
#
#  This module provides the following imported targets, if found:
#
#  Radare2::libr
#
#  This will define the following variables:
#  (but don't use them if you don't know what you are doing, use Radare2::libr)
#
#  Radare2_FOUND          - True if libr has been found.
#  Radare2_INCLUDE_DIRS   - libr include directory
#  Radare2_LIBRARIES      - List of libraries when using libr.
#  Radare2_LIBRARY_DIRS   - libr library directories
#
#  If libr was found using find_library and not pkg-config, the following variables will also be set:
#  Radare2_LIBRARY_<name> - Path to library r_<name>

if(WIN32)
	find_path(Radare2_INCLUDE_DIRS
			NAMES r_core.h r_bin.h r_util.h
			HINTS
				../radare2/include/libr
				../r2_dist/include/libr
				"$ENV{HOME}/bin/prefix/radare2/include/libr"
				/usr/local/include/libr
				/usr/include/libr)
        find_path(SDB_INCLUDE_DIR
			NAMES sdb.h sdbht.h sdb_version.h
			HINTS
				../radare2/include/libr/sdb
				../r2_dist/include/libr/sdb
				"$ENV{HOME}/bin/prefix/radare2/include/libr/sdb"
				/usr/local/include/libr/sdb
				/usr/include/libr/sdb)

        list(APPEND Radare2_INCLUDE_DIRS ${SDB_INCLUDE_DIR})

	set(Radare2_LIBRARY_NAMES
			core
			config
			cons
			io
			util
			flag
			asm
			debug
			hash
			bin
			lang
			io
			anal
			parse
			bp
			egg
			reg
			search
			syscall
			socket
			fs
			magic
			crypto)

	set(Radare2_LIBRARIES "")
	set(Radare2_LIBRARIES_VARS "")
	foreach(libname ${Radare2_LIBRARY_NAMES})
		find_library(Radare2_LIBRARY_${libname}
				r_${libname}
				HINTS
					../radare2/lib
					../r2_dist/lib
					"$ENV{HOME}/bin/prefix/radare2/lib"
					/usr/local/lib
					/usr/lib)

		list(APPEND Radare2_LIBRARIES ${Radare2_LIBRARY_${libname}})
		list(APPEND Radare2_LIBRARIES_VARS "Radare2_LIBRARY_${libname}")
	endforeach()

	set(Radare2_LIBRARY_DIRS "")

	add_library(Radare2::libr UNKNOWN IMPORTED)
	set_target_properties(Radare2::libr PROPERTIES
			IMPORTED_LOCATION "${Radare2_LIBRARY_core}"
			IMPORTED_LINK_INTERFACE_LIBRARIES "${Radare2_LIBRARIES}"
			INTERFACE_LINK_DIRECTORIES "${Radare2_LIBRARY_DIRS}"
			INTERFACE_INCLUDE_DIRECTORIES "${Radare2_INCLUDE_DIRS}")
	set(Radare2_TARGET Radare2::libr)
else()
	# support installation locations used by r2 scripts like sys/user.sh and sys/install.sh
	if(IAITO_USE_ADDITIONAL_RADARE2_PATHS)
		set(Radare2_CMAKE_PREFIX_PATH_TEMP ${CMAKE_PREFIX_PATH})
		list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/bin/prefix/radare2") # sys/user.sh
		list(APPEND CMAKE_PREFIX_PATH "/usr/local") # sys/install.sh
	endif()

	find_package(PkgConfig REQUIRED)
	if(CMAKE_VERSION VERSION_LESS "3.6")
		pkg_search_module(Radare2 REQUIRED r_core)
	else()
		pkg_search_module(Radare2 IMPORTED_TARGET REQUIRED r_core)
	endif()

	# reset CMAKE_PREFIX_PATH
	if(IAITO_USE_ADDITIONAL_RADARE2_PATHS)
		set(CMAKE_PREFIX_PATH ${Radare2_CMAKE_PREFIX_PATH_TEMP})
	endif()

	if((TARGET PkgConfig::Radare2) AND (NOT CMAKE_VERSION VERSION_LESS "3.11.0"))
		set_target_properties(PkgConfig::Radare2 PROPERTIES IMPORTED_GLOBAL ON)
		add_library(Radare2::libr ALIAS PkgConfig::Radare2)
		set(Radare2_TARGET Radare2::libr)
	elseif(Radare2_FOUND)
		add_library(Radare2::libr INTERFACE IMPORTED)
		set_target_properties(Radare2::libr PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${Radare2_INCLUDE_DIRS}")
		set_target_properties(Radare2::libr PROPERTIES
			INTERFACE_LINK_LIBRARIES "${Radare2_LIBRARIES}")
		set(Radare2_TARGET Radare2::libr)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Radare2 REQUIRED_VARS Radare2_TARGET Radare2_LIBRARIES Radare2_INCLUDE_DIRS)
