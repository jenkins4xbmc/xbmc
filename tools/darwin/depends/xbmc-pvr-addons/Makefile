include ../Makefile.include

SOURCE=../../../../xbmc-pvr-addons/

# configuration settings
export PATH:=$(TOOLCHAIN)/bin:$(PREFIX)/bin:$(PATH)
CONFIGURE=./configure --prefix=$(PREFIX) PKG_CONFIG_PATH=$(PREFIX)/lib/pkgconfig

# TODO checkout submodule?

all: compile

sync: configure
	cd $(SOURCE); make sync

configure:
	cd $(SOURCE); ./bootstrap
	cd $(SOURCE); $(CONFIGURE)

compile: configure
	cd $(SOURCE); make

clean:
	cd $(SOURCE); make clean

zip: compile
	cd $(SOURCE); make zip

install: compile
	cd $(SOURCE); make install