# Top-level SUBDIRS project.
#
# Using SUBDIRS allows qmake to build both the app and the background-sync
# daemon in a single "qmake + make" pass, which is required for the Sailfish
# SDK's mb2 shadow-build system (the build directory is separate from the
# source tree, so manually cd-ing into daemon/ from a spec %build section
# does not work).

TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = app daemon

# The app sub-project lives at the root of the source tree.
app.file    = harbour-imap-notes-app.pro

# The daemon sub-project lives in the daemon/ subdirectory.
daemon.subdir = daemon
