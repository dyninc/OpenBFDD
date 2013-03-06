#!/bin/sh
#
# Initial configuration utility for OpenBFDD project. 
# This is used only when building directly from the development repository. 
# This should not be needed to build from a distribution. 
#
set -e
set -x
aclocal 
autoheader 
automake --add-missing --copy --foreign
autoconf 
echo autogen completed
