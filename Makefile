CXX      = g++
CC       = gcc
OBJ      = main.o WebBrowser.o ConnHandler.o
LINKOBJ  = main.o WebBrowser.o ConnHandler.o
CFLAGS = -std=c++17 -Wall -g -c -Wno-parentheses
LIBS = `pkg-config gtkmm-3.0 webkit2gtk-4.0 --cflags --libs` `pkg-config --cflags --libs sigc++-2.0` -lboost_system -lboost_regex -lssl -lcrypto
BIN	 = examtool-browser

mkfile_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all all-before all-after clean clean-custom default
default: build ;
all: build ;

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)usr/bin/$(BIN)
	install -Dm755 examtool-browser.desktop $(DESTDIR)usr/share/applications/examtool-browser.desktop
	install -Dm755 examtool-browser-portal.desktop $(DESTDIR)usr/share/applications/examtool-browser-portal.desktop
	install -Dm644 exam-browser.svg $(DESTDIR)usr/share/icons/hicolor/scalable/apps/exam-browser.svg
	install -Dm644 exam-browser-normal.svg $(DESTDIR)usr/share/icons/hicolor/scalable/apps/exam-browser-normal.svg

delivery-build:
	rm -f *.crt *.csr *.key *.srl
	$(mkfile_dir)../gencert.sh -c $(mkfile_dir)../userspace -a $(mkfile_dir)browser
	sed -i '/\/\* BEGIN DAEMON CERT \*\//,/\/\* END DAEMON CERT \*\//{//!d}' Certificates.hpp
	awk '/\/\* END DAEMON CERT \*\//{while(getline line<"$(mkfile_dir)../daemon.crt"){printf("    \"%s\\n\"\n", line)}} //' Certificates.hpp > tmp
	mv tmp Certificates.hpp
	sed -i '/\/\* BEGIN BROWSER CERT \*\//,/\/\* END BROWSER CERT \*\//{//!d}' Certificates.hpp
	awk '/\/\* END BROWSER CERT \*\//{while(getline line<"$(mkfile_dir)browser.crt"){printf("    \"%s\\n\"\n", line)}} //' Certificates.hpp > tmp
	mv tmp Certificates.hpp
	sed -i '/\/\* BEGIN BROWSER KEY \*\//,/\/\* END BROWSER KEY \*\//{//!d}' Certificates.hpp
	awk '/\/\* END BROWSER KEY \*\//{while(getline line<"$(mkfile_dir)browser.key"){printf("    \"%s\\n\"\n", line)}} //' Certificates.hpp > tmp
	mv tmp Certificates.hpp

build: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(LINKOBJ) -o $(BIN) $(LIBS)

./%.o: ./%.cpp
	$(CXX) $(CFLAGS) $< -o $@ $(LIBS)

.PHONY : clean
clean :
	-rm -rf $(OBJ) $(BIN) usr/
