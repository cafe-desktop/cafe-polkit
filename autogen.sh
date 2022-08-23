#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="cafe-polkit"

(test -f $srcdir/src/Makefile.am) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"
    exit 1
}

which cafe-autogen || {
    echo "You need to install cafe-common from the MATE SVN repository"
    exit 1
}

REQUIRED_AUTOMAKE_VERSION=1.9
USE_MATE2_MACROS=1
USE_COMMON_DOC_BUILD=yes

. cafe-autogen
