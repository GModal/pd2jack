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
    ALIB_EXT = a
    PLATFORM = linux
    CFLAGS = -D__UNIX_JACK__ -D__LINUX_ALSA__
    EXEDIR = /usr/bin
    AUDIO_API = -ljack -lasound -lpthread -lm -ldl
  endif
endif

ifeq ($(DEBUG), true)
    CXXFLAGS = -g $(CFLAGS)
else
	CXXFLAGS = $(CFLAGS)
endif

LIBPD_DIR = ./libpd
LIBPDA = $(LIBPD_DIR)/libs/libpd.$(ALIB_EXT)

SRC_FILES = src/PdObject.cpp src/main.cpp src/pd2jack.hpp
TARGET = pd2jack

CXXFLAGS += -I$(LIBPD_DIR)/cpp -I$(LIBPD_DIR)/libpd_wrapper -I$(LIBPD_DIR)/libpd_wrapper/util -Wl,–export-dynamic \
           -I./src -std=c++11 -DLIBPD_USE_STD_MUTEX -O3

.PHONY: clean clobber libpd

$(TARGET): ${SRC_FILES:.cpp=.o} $(LIBPDA)
	g++ -o $(TARGET) $^ -L.$(LIBPDA) $(AUDIO_API)
ifeq ($(PLATFORM), mac)
	mkdir -p ./libs && cp $(LIBPD) ./libs
endif

clean:
	rm -f src/*.o

clobber: clean
	rm -f $(TARGET)
ifeq ($(PLATFORM), mac)
	rm -rf ./libs
endif
	cd $(LIBPD_DIR) && make clobber

install:
	install -m 755 $(TARGET) $(EXEDIR)

uninstall:
	rm -f $(EXEDIR)/$(TARGET)
	
libpd:
	cd libpd ; $(MAKE) STATIC=true
