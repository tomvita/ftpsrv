cmake_minimum_required(VERSION 3.13)

project(ftpsrv
    LANGUAGES C
    VERSION 0.1.1
    DESCRIPTION "small and fast ftp server"
)

# enable setting cmake options via set()
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# export compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# enable LTO
cmake_policy(SET CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

include(CheckCSourceCompiles)
include(CheckSymbolExists)

check_symbol_exists(IPTOS_THROUGHPUT
    "netinet/ip.h"
    HAVE_IPTOS_THROUGHPUT
)

check_symbol_exists(TCP_NODELAY
    "netinet/tcp.h"
    HAVE_TCP_NODELAY
)

check_symbol_exists(SO_KEEPALIVE
    "sys/socket.h"
    HAVE_SO_KEEPALIVE
)

check_symbol_exists(SO_OOBINLINE
    "sys/socket.h"
    HAVE_SO_OOBINLINE
)

check_symbol_exists(SO_REUSEADDR
    "sys/socket.h"
    HAVE_SO_REUSEADDR
)

check_c_source_compiles("
    #include <sys/stat.h>
    int main(void) { lstat(0, 0); }"
HAVE_LSTAT)

check_c_source_compiles("
    #include <unistd.h>
    int main(void) { readlink(0, 0, 0); }"
HAVE_READLINK)

check_c_source_compiles("
    #include <sys/sendfile.h>
    int main(void) { sendfile(0, 0, 0, 0); }"
HAVE_SENDFILE)

check_c_source_compiles("
    #include <pwd.h>
    int main(void) { getpwuid(0); }"
HAVE_GETPWUID)

check_c_source_compiles("
    #include <grp.h>
    int main(void) { getgrgid(0); }"
HAVE_GETGRGID)

check_c_source_compiles("
    #include <poll.h>
    int main(void) { poll(0, 0, 0); }"
HAVE_POLL)

check_c_source_compiles("
    #include <string.h>
    int main(void) { strncasecmp(0, 0, 0); }"
HAVE_STRNCASECMP)

function(fetch_minini)
    FetchContent_Declare(minIni
        GIT_REPOSITORY https://github.com/ITotalJustice/minIni-nx.git
        GIT_TAG 63ec295
    )

    set(MININI_USE_NX ${NINTENDO_SWITCH})
    set(MININI_USE_STDIO NOT ${NINTENDO_SWITCH})
    set(MININI_USE_FLOAT OFF)

    FetchContent_MakeAvailable(minIni)
endfunction(fetch_minini)

function(ftp_set_compile_definitions target)
    target_compile_definitions(${target} PRIVATE
        HAVE_READLINK=$<BOOL:${HAVE_READLINK}>
        HAVE_SENDFILE=$<BOOL:${HAVE_SENDFILE}>
        HAVE_GETPWUID=$<BOOL:${HAVE_GETPWUID}>
        HAVE_GETGRGID=$<BOOL:${HAVE_GETGRGID}>
        HAVE_STRNCASECMP=$<BOOL:${HAVE_STRNCASECMP}>
        HAVE_POLL=$<BOOL:${HAVE_POLL}>
        HAVE_IPTOS_THROUGHPUT=$<BOOL:${HAVE_IPTOS_THROUGHPUT}>
        HAVE_TCP_NODELAY=$<BOOL:${HAVE_TCP_NODELAY}>
        HAVE_SO_KEEPALIVE=$<BOOL:${HAVE_SO_KEEPALIVE}>
        HAVE_SO_OOBINLINE=$<BOOL:${HAVE_SO_OOBINLINE}>
        HAVE_SO_REUSEADDR=$<BOOL:${HAVE_SO_REUSEADDR}>
    )
endfunction(ftp_set_compile_definitions)

function(ftp_set_options target path_size sessions buf_size)
    # path size is -1
    math(EXPR sscanf_val "${path_size} - 1" OUTPUT_FORMAT DECIMAL)
    # add base defs
    ftp_set_compile_definitions(${target})
    # add the rest
    target_compile_definitions(${target} PRIVATE
        FTP_PATHNAME_SIZE=${path_size}
        FTP_PATHNAME_SSCANF="${sscanf_val}"
        FTP_MAX_SESSIONS=${sessions}
        FTP_FILE_BUFFER_SIZE=${buf_size}
    )
endfunction(ftp_set_options)

function(ftp_add name)
    set_target_properties(${name} PROPERTIES
        C_STANDARD 99
        C_EXTENSIONS ON
    )

    target_compile_options(${name} PRIVATE
        $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:GNU>>:
            -Wall
        >
        $<$<C_COMPILER_ID:MSVC>:
            /W4
        >
    )
endfunction(ftp_add)

add_library(ftpsrv src/ftpsrv.c)
target_include_directories(ftpsrv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
ftp_add(ftpsrv)
ftp_set_compile_definitions(ftpsrv)

if (NINTENDO_SWITCH)
    ftp_set_options(ftpsrv 769 256 1024*64)
    fetch_minini()

    target_compile_definitions(ftpsrv PUBLIC
        FTP_VFS_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nx/vfs_nx.h"
        FTP_SOCKET_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/socket_unistd.h"
        VFS_NX_BUFFER_WRITES=1
    )

    add_executable(ftpexe
        src/platform/nx/main.c
        src/platform/nx/vfs_nx.c
        src/platform/nx/utils.c
    )
    ftp_add(ftpexe)
    target_link_libraries(ftpexe PRIVATE ftpsrv minIni)

    nx_generate_nacp(
        OUTPUT ftpexe.nacp
        NAME ${CMAKE_PROJECT_NAME}
        AUTHOR TotalJustice
        VERSION ${CMAKE_PROJECT_VERSION}
    )

    nx_create_nro(ftpexe
        OUTPUT ${CMAKE_BINARY_DIR}/ftpexe.nro
        NACP ftpexe.nacp
    )

    add_library(ftpsrv_sysmod src/ftpsrv.c)
    target_include_directories(ftpsrv_sysmod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    ftp_add(ftpsrv_sysmod)
    ftp_set_options(ftpsrv_sysmod 769 6 1024*16)

    target_compile_definitions(ftpsrv_sysmod PUBLIC
        FTP_VFS_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nx/vfs_nx.h"
        FTP_SOCKET_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/socket_unistd.h"
        VFS_NX_BUFFER_WRITES=0
    )

    add_executable(sysftp
        src/platform/nx/main_sysmod.c
        src/platform/nx/vfs_nx.c
        src/platform/nx/utils.c
    )
    ftp_add(sysftp)
    target_link_libraries(sysftp PRIVATE ftpsrv_sysmod minIni)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/420000000000011B)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/420000000000011B/flags)

    add_custom_command(
        TARGET ftpexe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nx/toolbox.json
            ${CMAKE_CURRENT_BINARY_DIR}/420000000000011B/toolbox.json

        COMMAND ${CMAKE_COMMAND} -E touch
            ${CMAKE_CURRENT_BINARY_DIR}/420000000000011B/flags/boot2.flag
    )

    nx_create_exefs(sysftp
        OUTPUT 420000000000011B/exefs.nsp
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/nx/sysftp.json
    )
elseif(NINTENDO_DS)
    ftp_set_options(ftpsrv 769 16 1024*64)
    fetch_minini()

    target_compile_definitions(ftpsrv PUBLIC
        FTP_VFS_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/vfs_unistd.h"
        FTP_SOCKET_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/socket_unistd.h"
    )

    add_executable(ftpexe
        src/platform/nds/main.c
        src/platform/unistd/vfs_unistd.c
    )
    target_link_libraries(ftpexe PRIVATE ftpsrv minIni fat dswifi9)
    ftp_add(ftpexe)

    nds_create_rom(ftpexe
        NAME "FTPSrv"
        SUBTITLE1 "FTPS"
        SUBTITLE2 "TJ"
    )
elseif(NINTENDO_3DS)
    ftp_set_options(ftpsrv 769 64 1024*64)
    fetch_minini()

    target_compile_definitions(ftpsrv PUBLIC
        FTP_VFS_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/vfs_unistd.h"
        FTP_SOCKET_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/socket_unistd.h"
    )

    add_executable(ftpexe
        src/platform/3ds/main.c
        src/platform/unistd/vfs_unistd.c
    )
    target_link_libraries(ftpexe PRIVATE ftpsrv minIni)
    ftp_add(ftpexe)

    ctr_generate_smdh(${PROJECT_NAME}.smdh
        NAME "${PROJECT_NAME}"
        DESCRIPTION "v${PROJECT_VERSION}"
        AUTHOR "TotalJustice"
    )

    ctr_create_3dsx(ftpexe
        SMDH ${PROJECT_NAME}.smdh
    )
elseif(NINTENDO_WII)
    ftp_set_options(ftpsrv 769 10 1024*64)
    fetch_minini()

    target_compile_definitions(ftpsrv PUBLIC
        FTP_VFS_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/vfs_unistd.h"
        FTP_SOCKET_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/wii/socket_wii.h"
    )

    add_executable(ftpexe
        src/platform/wii/main.c
        src/platform/unistd/vfs_unistd.c
    )
    target_link_libraries(ftpexe PRIVATE ftpsrv fat minIni)
    ftp_add(ftpexe)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/apps/ftpsrv)
    ogc_create_dol(ftpexe)

    add_custom_command(
        TARGET ftpexe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/wii/icon.png
            ${CMAKE_CURRENT_BINARY_DIR}/apps/ftpsrv/icon.png

        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/wii/meta.xml
            ${CMAKE_CURRENT_BINARY_DIR}/apps/ftpsrv/meta.xml

        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/ftpexe.dol
            ${CMAKE_CURRENT_BINARY_DIR}/apps/ftpsrv/boot.dol
    )
else()
    target_compile_definitions(ftpsrv PRIVATE
        FTP_FILE_BUFFER_SIZE=1024*512
    )
    target_compile_definitions(ftpsrv PUBLIC
        FTP_VFS_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/vfs_unistd.h"
        FTP_SOCKET_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/src/platform/unistd/socket_unistd.h"
        FTP_VFS_FD=1
    )

    add_executable(ftpexe
        src/platform/unistd/main.c
        src/platform/unistd/vfs_unistd.c
        src/args/args.c
    )
    target_link_libraries(ftpexe PRIVATE ftpsrv)
    ftp_add(ftpexe)
endif()
