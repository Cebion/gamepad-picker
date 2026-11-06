ARCH ?= aarch64
CC = gcc
STRIP = strip

# aarch64 uses its own _start so the binary only needs GLIBC_2.17 (see start_aarch64.S).
START_aarch64 = -nostartfiles start_aarch64.S

gamepad-picker.$(ARCH): gamepad-picker.c stb_truetype.h fonts/portmsg_regular.h fonts/portmsg_bold.h $(wildcard start_$(ARCH).S)
	$(CC) -O2 -Wall -Wno-unused-function -o $@ $(START_$(ARCH)) gamepad-picker.c $$(sdl2-config --cflags --libs) -lm
	$(STRIP) $@

fonts/portmsg_regular.h: fonts/PortmsgSans-Regular.ttf
	( echo 'static const unsigned char portmsg_regular_ttf[] = {'; xxd -i < $<; echo '};' ) > $@

fonts/portmsg_bold.h: fonts/PortmsgSans-Bold.ttf
	( echo 'static const unsigned char portmsg_bold_ttf[] = {'; xxd -i < $<; echo '};' ) > $@

clean:
	rm -f gamepad-picker.$(ARCH) fonts/*.h

.PHONY: clean
