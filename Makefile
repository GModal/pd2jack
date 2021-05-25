# 
# copyright (c) 2014 rafael vega <rvega@elsoftwarehamuerto.org>
# 
# bsd simplified license.
# for information on usage and redistribution, and for a disclaimer of all
# warranties, see the file, "license.txt," in this distribution.
# 
# see https://github.com/libpd/libpd for documentation
#

# detect platform, move libpd dylib to local folder on mac
UNAME = $(shell uname)
SOLIB_PREFIX = lib

ifeq ($(UNAME), Darwin)  # Mac
  SOLIB_EXT = dylib
  PLATFORM = mac
  CXXFLAGS = -D__MACOSX_CORE__ -DHAVE_UNISTD_H
  AUDIO_API = -framework Foundation -framework CoreAudio
else
  ifeq ($(OS), Windows_NT)  # Windows, use Mingw
    SOLIB_EXT = dll
    SOLIB_PREFIX = 
    PLATFORM = windows
    CXXFLAGS = -D__WINDOWS_DS__
    AUDIO_API = -lole32 -loleaut32 -ldsound -lwinmm
  else  # assume Linux
    SOLIB_EXT = so
    PLATFORM = linux
    EXEDIR = /usr/bin
    CXXFLAGS = -D__UNIX_JACK__ -D__LINUX_ALSA__
    AUDIO_API = -ljack -lasound -pthread
  endif
endif

LIBPD_DIR = /usr/local
LIBPD = $(LIBPD_DIR)/lib/libpd.$(SOLIB_EXT)

SRC_FILES = src/PdObject.cpp src/main.cpp
TARGET = pd2jack

CXXFLAGS = -I$(LIBPD_DIR)/include/libpd/util -I$(LIBPD_DIR)/include/libpd \
           -I./src -std=c++11 -DLIBPD_USE_STD_MUTEX -O3

.PHONY: clean clobber

$(TARGET): ${SRC_FILES:.cpp=.o} $(LIBPD)
	g++ -o $(TARGET) $^ $(LIBPD) $(AUDIO_API)
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
