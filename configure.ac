
AC_INIT([ncdc], [1.13g], [projects@yorhel.nl])
AC_CONFIG_SRCDIR([src/ncdc.h])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects])
PKG_PROG_PKG_CONFIG([0.18])


# Check for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

AC_SYS_LARGEFILE


# Use silent building
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Check for pod2man
AC_CHECK_PROG([have_pod2man],[pod2man],[yes],[no])
AM_CONDITIONAL([USE_POD2MAN], [test "x$have_pod2man" = "xyes"])

# Check for makeheaders. If the system does not provide it, compile our own copy in deps/
AC_CHECK_PROG([have_mh],[makeheaders],[yes],[no])
AM_CONDITIONAL([HAVE_MH], [test "x$have_mh" = "xyes"])


# Check for header files.
AC_CHECK_HEADERS(
  [wchar.h locale.h signal.h unistd.h fcntl.h zlib.h bzlib.h\
   sys/types.h sys/stat.h sys/socket.h\
   arpa/inet.h netdb.h netinet/in.h netinet/tcp.h],[],
  AC_MSG_ERROR([Required header file not found]))

have_nch=no
AC_CHECK_HEADERS([ncursesw/ncurses.h ncurses/ncurses.h ncurses.h],[have_nch=yes])
if test "x$have_nch" = "xno"; then
  AC_MSG_ERROR([Could not find a header files for ncurses.])
fi


# Require a large off_t
AC_CHECK_SIZEOF([off_t])
if test "$ac_cv_sizeof_off_t" -lt 8; then
  AC_MSG_ERROR([No large file support available in this system.])
fi


# Check for library functions.
AC_CHECK_FUNCS(
  [setlocale sigaction fcntl ftruncate],[],
  AC_MSG_ERROR([Required function missing]))

# Check for posix_fadvise()
AC_CHECK_FUNCS([posix_fadvise])

AC_SEARCH_LIBS([inet_pton], [nsl])
AC_SEARCH_LIBS([socket], [socket], [], [
  AC_CHECK_LIB([socket], [socket], [LIBS="-lsocket -lnsl $LIBS"], [], [-lnsl])])


# Check for sendfile() support (not required)
# The following checks are based on ProFTPD's configure.in, except ncdc only
# supports the Linux and BSD variant at the moment, as those are the only two I
# have tested so far.

AC_CACHE_CHECK([which sendfile() implementation to use], pr_cv_sendfile_func,
  pr_cv_sendfile_func="none"

  # Linux
  if test "$pr_cv_sendfile_func" = "none"; then
    AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [[ #include <sys/types.h>
         #include <sys/sendfile.h>
         #include <unistd.h> ]],
      [[ int i=0;
         off_t o=0;
         size_t c=0;
         (void)sendfile(i,i,&o,c); ]])],
      [pr_cv_sendfile_func="Linux"])
  fi

  # BSD
  if test "$pr_cv_sendfile_func" = "none"; then
    AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [[ #include <sys/types.h>
         #include <sys/socket.h>
         #include <sys/uio.h> ]],
      [[ int i=0;
         off_t o=0;
         size_t n=0;
         struct sf_hdtr h={};
         (void)sendfile(i,i,o,n,&h,&o,i); ]])],
      [pr_cv_sendfile_func="BSD"])
  fi
)

# set defines
if test "$pr_cv_sendfile_func" != none; then
  AC_DEFINE(HAVE_SENDFILE, 1, [Define if sendfile support.])
fi

case "$pr_cv_sendfile_func" in
  "Linux")
    AC_DEFINE(HAVE_LINUX_SENDFILE, 1, [Define if using Linux sendfile support.]);;
  "BSD")
    AC_DEFINE(HAVE_BSD_SENDFILE,   1, [Define if using BSD sendfile support.]);;
esac


# Check for ncurses
AC_CHECK_LIB([ncursesw], [get_wch], [
    AC_SUBST([NCURSES_LIBS],[-lncursesw])
  ], [
    AC_CHECK_LIB([ncurses],
                 [get_wch],
                 [AC_SUBST([NCURSES_LIBS],[-lncurses])],
                 [AC_MSG_ERROR(ncursesw library is required)])
  ]
)

# Check for zlib
AC_CHECK_LIB([z],
             [deflate],
             [AC_SUBST([Z_LIBS],[-lz])],
             [AC_MSG_ERROR(zlib library is required)])

# Check for libbz2
AC_CHECK_LIB([bz2],
             [BZ2_bzReadOpen],
             [AC_SUBST([BZ2_LIBS],[-lbz2])],
             [AC_MSG_ERROR(bzip2 library is required)])

# Check for SQLite3
PKG_CHECK_EXISTS([sqlite3],[
    PKG_CHECK_MODULES([SQLITE],[sqlite3])
  ],[
    AC_CHECK_HEADERS([sqlite3.h],[],
                     [AC_MSG_ERROR([sqlite3 header file not found])])
    AC_CHECK_LIB([sqlite3],
                 [sqlite3_open],
                 [AC_SUBST([SQLITE_LIBS],[-lsqlite3])],
                 [AC_MSG_ERROR([sqlite3 library is required])])
  ]
)

# Check for modules
PKG_CHECK_MODULES([GLIB],   [glib-2.0 >= 2.24 gthread-2.0])
PKG_CHECK_MODULES([GNUTLS], [gnutls >= 2.4])

have_sudp=no
PKG_CHECK_EXISTS([gnutls >= 2.12], [have_sudp=yes])



# Check whether we should use the version string from AC_INIT(), or use
# git-describe to create one. This trick is copied from the pacman source.

AC_ARG_ENABLE(git-version,
  AS_HELP_STRING([--enable-git-version], [enable use of git version in version string if available]),
  [wantgitver=$enableval], [wantgitver=yes])

usegitver=no
if test "x$wantgitver" = "xyes" ; then
  AC_CHECK_PROGS([GIT], [git], [no])
  test "x$GIT" != "xno" -a -d "$srcdir/.git" && usegitver=yes
fi
AM_CONDITIONAL(USE_GIT_VERSION, test "x$usegitver" = "xyes")





AC_OUTPUT([Makefile])


# Information about SUDP support
echo ""
if test "x$have_sudp" = "xyes"; then
  echo "encrypted UDP : enabled."
else
  echo "encrypted UDP : disabled. (Requires gnutls >= 2.12.0)"
fi
echo ""
