dnl Process this file with autoconf to produce a configure script.
AC_INIT([lcrp], [m4_esyscmd(./version-gen.sh)], [lixi@ddn.com], [lcrp])
AC_CONFIG_SRCDIR(./src/lcrp.c)
AC_CONFIG_HEADERS(config.h)

AM_INIT_AUTOMAKE([tar-pax dist-bzip2 foreign])
AM_EXTRA_RECURSIVE_TARGETS([check_clean])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_LANG(C)

AC_PREFIX_DEFAULT([/usr])

AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_CC_C_O
AM_CONDITIONAL(COMPILER_IS_GCC, test "x$GCC" = "xyes")

AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_LEX
AC_PROG_YACC
PKG_PROG_PKG_CONFIG

#
# MB_CHECK_FILE
#
# Check for file existance even when cross compiling
# $1 - file to check
# $2 - do 'yes'
# $3 - do 'no'
#
AC_DEFUN([MB_CHECK_FILE], [
AS_VAR_PUSHDEF([mb_file], [mb_cv_file_$1])dnl
AC_CACHE_CHECK([for $1], mb_file, [
AS_IF([test -r "$1"],
	[AS_VAR_SET(mb_file, [yes])],
	[AS_VAR_SET(mb_file, [no])])
])
AS_IF([test AS_VAR_GET(mb_file) = yes], [$2], [$3])[]dnl
AS_VAR_POPDEF([mb_file])dnl
]) # MB_CHECK_FILE

#
# MB_ARG_REPLACE_PATH(PACKAGE, PATH)
#
AC_DEFUN([MB_ARG_REPLACE_PATH], [
new_configure_args=
eval set -- $ac_configure_args
for arg; do
	case $arg in
		--with-[$1]=*)
			arg=--with-[$1]=[$2] ;;
		*\'*)
			arg=$(printf %s\n ["$arg"] | \
				sed "s/'/'\\\\\\\\''/g") ;;
	esac
	dnl AS_VAR_APPEND([new_configure_args], [" '$arg'"])
	new_configure_args="$new_configure_args '$arg'"
done
ac_configure_args=$new_configure_args
])

#
# __MB_ARG_CANON_PATH
#
# this is the work-horse of the next function
#
AC_DEFUN([__MB_ARG_CANON_PATH], [
	[$3]=$(readlink -f $with_$2)
	MB_ARG_REPLACE_PATH([$1], $[$3])
])

#
# MB_ARG_CANON_PATH
#
# a front-end for the above function that transforms - and . in the
# PACKAGE portion of --with-PACKAGE into _ suitable for variable names
#
AC_DEFUN([MB_ARG_CANON_PATH], [
	__MB_ARG_CANON_PATH([$1], m4_translit([$1], [-.], [__]), [$2])
])

dnl We may be able to use older versions, but I have not verified that
PKG_CHECK_MODULES([ext2fs], [ext2fs >= 1.42.7 com_err >= 1.42.7])
PKG_CHECK_MODULES([json_c], [json-c >= 0.11])

AC_CHECK_HEADER([lustre/lustreapi.h], , AC_MSG_ERROR([liblustreapi is required]))

AC_CHECK_HEADER([linux/lustre/lustre_user.h],
                [new_user_header=yes],)

AC_CHECK_HEADER([lustre/lustre_user.h],
                [idle_user_header=yes],)

if test "x$new_user_header" = "x" -a "x$idle_user_header" = "x"; then
        AC_MSG_ERROR([lustre_user.h is required])
fi

if test "x$new_user_header" = "xyes"; then
        AC_DEFINE(NEW_USER_HEADER, 1, [Lustre uses new user header])
fi

if test "x$idle_user_header" = "xyes"; then
        AC_DEFINE(IDLE_USER_HEADER, 1, [Lustre uses idle user header])
fi

# -------- check whether DoM is supported by Lustre head files --------
AC_MSG_CHECKING([Lustre has PFL support])
saved_libs=$LIBS
LIBS="-llustreapi"
AC_LINK_IFELSE([AC_LANG_SOURCE([	
	#include <lustre/lustre_user.h>

	int main(void) {
		int a = LOV_USER_MAGIC_COMP_V1;
	}
])],[
AC_DEFINE([HAVE_LUSTRE_PFL], 1,
	  [Lustre has PFL support])
have_lustre_pfl=yes
], [have_lustre_pfl="no"])
LIBS=$saved_libs
AC_MSG_RESULT([$have_lustre_pfl])

# -------- check for llapi_layout_get_by_xattr() --------
AC_MSG_CHECKING([Lustre have llapi_layout_get_by_xattr() and LSoM])
saved_libs=$LIBS
LIBS="-llustreapi"
AC_LINK_IFELSE([AC_LANG_SOURCE([	
	#include <lustre/lustreapi.h>

	int main(void) {
		llapi_layout_get_by_xattr(NULL, 0, 0);
	}
])],[
AC_DEFINE([HAVE_LAYOUT_BY_XATTR], 1,
	  [have llapi_layout_get_by_xattr()])
AC_DEFINE([HAVE_LAZY_SIZE_ON_MDT], 1,
	  [have lazy size on MDT])
have_layout_by_xattr=yes
], [have_layout_by_xattr="no"])
LIBS=$saved_libs
AC_MSG_RESULT([$have_layout_by_xattr])

# -------- check for build laudit --------
BUILD_LAUDIT="yes"
NIDSTR=""
AC_CHECK_HEADER([lnet/nidstr.h], NIDSTR="lnet", [], [
AC_INCLUDES_DEFAULT
#include <stdbool.h>
])
AS_IF([test "x$NIDSTR" = "xlnet" ],
      [AC_DEFINE([HAVE_LNET_NIDSTR], [1], [lnet/nidstr.h exists]) lustre_user_dir="lustre"],
      [AC_CHECK_HEADER([linux/lnet/nidstr.h], NIDSTR="linux", [], [
AC_INCLUDES_DEFAULT
#include <stdbool.h>
])])
AS_IF([test "x$NIDSTR" = "xlinux" ],
      [AC_DEFINE([HAVE_LINUX_NIDSTR], [1], [linux/lnet/nidstr.h exists])] lustre_user_dir="linux/lustre")
if test "x$NIDSTR" = "x"; then
	AC_MSG_WARN([])
	AC_MSG_WARN([nidstr include is needed for laudit. laudit will not be built.])
	AC_MSG_WARN([])
	BUILD_LAUDIT="no"
fi

if test "x$BUILD_LAUDIT" = "xyes"; then
AC_MSG_CHECKING([whether Lustre is audit-capable])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
	#include <$lustre_user_dir/lustre_user.h>

	int main(void) {
		enum changelog_rec_type type1 = CL_GETXATTR;
		enum changelog_rec_type type2 = CL_DN_OPEN;
	}
])],[audit_capable="yes"],[audit_capable="no"])
AC_MSG_RESULT([$audit_capable])
if test "x$audit_capable" = "xno"; then
	AC_MSG_WARN([])
	AC_MSG_WARN([Lustre must be audit-capable for laudit. laudit will not be built.])
	AC_MSG_WARN([])
	BUILD_LAUDIT="no"
fi
fi

AC_SUBST(BUILD_LAUDIT)
AM_CONDITIONAL(BUILD_LAUDIT, test "$BUILD_LAUDIT" = "yes")

# -------- check for distro version --------
AC_MSG_CHECKING([for distro version])
DISTRO=$(sh detect-distro.sh)
DISTRO_NAME=$(echo $DISTRO | awk -F '-' '{print $1}')
if [[[ "$DISTRO_NAME" != "rhel" ]]]; then
        AC_MSG_ERROR([$DISTRO_NAME is not a supported distro.])
fi
DISTRO_RELEASE=$(echo $DISTRO | awk -F 'rhel-' '{print $2}' | awk -F '.' '{print $1}')
AC_MSG_RESULT([$DISTRO_RELEASE])
AC_SUBST(DISTRO_RELEASE)

# ------- check for python --------
AC_CHECK_PROGS([PYTHON], [python], [])
if test "x$PYTHON" = "x"; then
	AC_MSG_ERROR([Python is needed for tests. Install python please.])
fi

# ------- check for pep8 --------
AC_CHECK_PROGS([PEP8], [pep8], [])
if test "x$PEP8" = "x"; then
	AC_MSG_ERROR([pep8 is needed to check python coding style. Install pep8 please.])
fi

# ------- check for pylint --------
AC_CHECK_PROGS([PYLINT], [pylint], [])
if test "x$PYLINT" = "x"; then
	AC_MSG_ERROR([pylint is needed to check python coding style. Install pylint please.])
fi

# ------- check for protoc --------
AC_CHECK_PROGS([PROTOC], [protoc], [])
if test "x$PROTOC" = "x"; then
	AC_MSG_ERROR([protoc is needed to compile Protobuf file. Install protoc please.])
fi

# -------- check whether enable zfs support --------
AC_MSG_CHECKING([whether enable zfs support])
AC_ARG_ENABLE([zfs],
	AC_HELP_STRING([--enable-zfs],
                       [enable zfs support]),
        [], [enable_zfs="no"])
AC_MSG_RESULT([$enable_zfs])
AM_CONDITIONAL([ZFS], [test x$enable_zfs = xyes])

AS_IF([test "x$enable_zfs" != xno ], [
PKG_CHECK_MODULES([zfs], [libzfs], AC_DEFINE([HAVE_ZFS], [1], [enable ZFS support])]))

AC_CHECK_HEADER([libzfs/libzutil.h],
                [AC_DEFINE(HAVE_LIBZUTIL, 1, [libzutil.h exists])])
# -------- check whether enable LPCC support for copytool --------
AC_MSG_CHECKING([whether enable LPCC support for copytool])
AC_ARG_ENABLE([copytool-lpcc],
        AC_HELP_STRING([--enable-copytool-lpcc],
                       [enable LPCC support for copytool]),
        [], [enable_copytool_lpcc="no"])
AC_MSG_RESULT([$enable_copytool_lpcc])
AM_CONDITIONAL([COPYTOOL_LPCC], [test x$enable_copytool_lpcc = xyes])
AS_IF([test "x$enable_copytool_lpcc" = xyes ],
      [AC_DEFINE([HAVE_COPYTOOL_LPCC], [1], [enable LPCC support for copytool])])

# -------- check for cached ISO --------
AC_MSG_CHECKING([for cached ISO])
AC_ARG_WITH([cached-iso],
	AC_HELP_STRING([--with-cached-iso=path],
		       [set path to cached ISO]),
	[MB_ARG_CANON_PATH([cached-iso], [CACHED_ISO_PATH])],
	[])
AC_MSG_RESULT([$CACHED_ISO_PATH])
AC_SUBST(CACHED_ISO_PATH)

AS_IF([test "x$CACHED_ISO_PATH" != "x"],
      [MB_CHECK_FILE([$CACHED_ISO_PATH], [],
                     [AC_MSG_ERROR([cached ISO dir $CACHED_ISO_PATH could not be found.])])])

dnl Pull the needed libraries into LIBS (needed for the AC_LINK_IFELSE below)
dnl These should never fail if the PKG_CHECK above passes
AC_SEARCH_LIBS([com_err], [com_err])
AC_SEARCH_LIBS([ext2fs_open2], [ext2fs])

LCRP_RELEASE="1"
AC_DEFINE_UNQUOTED(RELEASE, "$LCRP_RELEASE", [release info] )
AC_SUBST(LCRP_RELEASE)

# for exporting to spec file
AC_SUBST(ac_configure_args)
AC_CONFIG_FILES([Makefile
                 lcrp.spec
                 src/Makefile])
AC_OUTPUT
