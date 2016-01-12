lcd2usb test application  -  http://www.harbaum.org/till/lcd2usb
----------------------------------------------------------------

This simple test application is meant to demonstrate libusb
interfacing to the lcd2usb interface.

This is no useful application, if you are only interesting in 
using the lcd2usb interface please check out the latest version 
of lcd4linux. 

Linux
-----

This demo application has been developed under and for linux. Just
make sure you have libusb installed. To use this program just
compile by typing "make" and run the resulting lcd2usb.

Windows
-------

This program can be compiled for windows. This has been tested 
under Linux using xmingw and the windows port of libusb 
(see http://libusb-win32.sourceforge.net). To install the 
driver plug the device in and install the driver from
the win directory. Then run testapp/lcd2usb.exe

This program may also be compiled under windows using cygwin or
mingw (which is part of cygwin). In order to use cygwin simply 
copy usb.h win32-linusb to /cygwin/usr/include and libusb.a to
/cygwin/lib and do a "make -f Makefile.cygwin". Don't forget to 
distribute /cygwin/bin/cygwin1.dll with your file to allow it to 
run in non-cygwin environments as well. No dll is required when using 
mingw. In that case copy usb.h to /cygwin/usr/include/mingw and 
libusb.a to /cygwin/lib/mingw. Finally do a "make -f Makefile.mingw".

MacOS X
-------

The program can be compiled under MacOS as well. The fink version
of linusb has to be installed and a simple "make -f Makefile.macos"
will build the native MacOS X version.
