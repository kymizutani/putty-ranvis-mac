/*
 * This header file provides the various versioning-related #defines
 * for a particular PuTTY build.
 *
 * When my automated build system does a full build, Buildscr
 * completely overwrites this file with information derived from the
 * circumstances and type of that build. The information _here_ is
 * default stuff used for local development runs of 'make'.
 *
 * For this macOS port, TEXTVER is what the About box and the
 * command-line --version say, and it's also the version the .app
 * bundles claim (unix/CMakeLists.txt reads it out of this file). The
 * number after the hyphen is the port build number, and it's the same
 * number that the distributed .dmg and .zip file names end with -
 * putty-ranvis-0.85-macos-arm64-002.dmg is a build of 0.85-002.
 */

#define TEXTVER "0.85-002"
#define SSHVER "-ranvis-0.85-002"
#define BINARY_VERSION 0,85,0,2
