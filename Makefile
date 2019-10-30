all: dll exe

.PHONY: dll exe

dll:
	gcc -m32 -O3 -c -o pw_tab.o pw_tab.c
	gcc -m32 -o pw_tab.dll -s -shared pw_tab.o -Wl,--subsystem,windows

exe:
	gcc -m32 -O3 -o elementclient.exe pw_wrapper.c
