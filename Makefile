#  Original Makefile for libpd by:
# copyright (c) 2014 rafael vega <rvega@elsoftwarehamuerto.org>
# 
# bsd simplified license.
# for information on usage and redistribution, and for a disclaimer of all
# warranties, see the file, "license.txt," in this distribution.
#     see https://github.com/libpd/libpd for documentation
#
# for "pd2jack"
#   by Doug Garmon
#
# The Mac & Windows conditionals & compilation will almost certainly NOT work -- sorry. 
#     But they included as a starting point...
#

# detect platform, move libpd dylib to local folder on mac
UNAME = $(shell uname)
SOLIB_PREFIX = lib

ifeq ($(UNAME), Darwin)  # Mac
  SOLIB_EXT = dylib
  PLATFORM = mac
  CFLAGS = -D__MACOSX_CORE__ -DHAVE_UNISTD_H
  AUDIO_API = -framework Foundation -framework CoreAudio
else
  ifeq ($(OS), Windows_NT)  # Windows, use Mingw
    SOLIB_EXT = dll
    SOLIB_PREFIX = 
    PLATFORM = windows
    CFLAGS = -D__WINDOWS_DS__
    AUDIO_API = -lole32 -loleaut32 -ldsound -lwinmm
  else  # assume Linux
    SOLIB_EXT = so
    alib_ext = a
    PLATFORM = linux
    prefix = /usr
    exec_prefix = $(prefix)
    bindir = $(exec_prefix)/bin
    CFLAGS = -D__UNIX_JACK__ -D__LINUX_ALSA__
    AUDIO_API = -ljack -lasound -lpthread -lm -ldl
  endif
endif

ifeq ($(DEBUG), true)
    CXXFLAGS = -g $(CFLAGS)
else
	CXXFLAGS = $(CFLAGS)
endif

LIBPD_DIR = ./libpd
LIBPDA = $(LIBPD_DIR)/libs/libpd.$(alib_ext)

SRC_FILES = src/PdObject.cpp src/main.cpp src/pd2jack.hpp
TARGET = pd2jack

CXXFLAGS += -I$(LIBPD_DIR)/cpp -I$(LIBPD_DIR)/libpd_wrapper -I$(LIBPD_DIR)/libpd_wrapper/util -Wl,–export-dynamic \
           -I./src -std=c++11 -DLIBPD_USE_STD_MUTEX -O3

.PHONY: clean clobber

$(TARGET): ${SRC_FILES:.cpp=.o} $(LIBPDA) 
	g++ -o $(TARGET) $^ -L.$(LIBPDA) $(AUDIO_API)
ifeq ($(PLATFORM), mac)
	mkdir -p ./libs && cp $(LIBPD) ./libs
endif

$(LIBPDA):
	cd libpd ; $(MAKE) STATIC=true EXTRA=true UTIL=true

clean:
	rm -f src/*.o

clobber: clean
	rm -f $(TARGET)
ifeq ($(PLATFORM), mac)
	rm -rf ./libs
endif
	cd $(LIBPD_DIR) && make clobber

install:
	install -m 755 $(TARGET) $(bindir)

uninstall:
	rm -f $(EXEDIR)/$(TARGET)
	
