# makefile of pianobar

PKG_CONFIG?=pkg-config
PROGRAM_BASE:=signalbox
HOST_OS:=$(shell uname -s 2>/dev/null)
ifneq ($(filter Windows_NT MINGW% MSYS%,$(OS) $(HOST_OS)),)
	WINDOWS:=1
	EXEEXT:=.exe
endif
PROGRAM:=$(PROGRAM_BASE)$(EXEEXT)
PREFIX:=/usr/local
BINDIR:=${PREFIX}/bin
LIBDIR:=${PREFIX}/lib
INCDIR:=${PREFIX}/include
MANDIR:=${PREFIX}/share/man
DYNLINK:=0
CFLAGS?=-O2 -DNDEBUG
MACOS_CODESIGN_IDENTITY?=
MACOS_CODESIGN_IDENTIFIER?=org.signalbox.signalbox

ifeq (${CC},cc)
	ifeq (${WINDOWS},1)
		CC:=gcc -std=c99
	else ifeq (${HOST_OS},Darwin)
		CC:=gcc -std=c99
	else ifeq (${HOST_OS},FreeBSD)
		CC:=cc -std=c99
	else ifeq (${HOST_OS},OpenBSD)
		CC:=cc -std=c99
	else
		CC:=c99
	endif
endif

PIANOBAR_DIR:=src
PIANOBAR_SRC:=\
		${PIANOBAR_DIR}/main.c \
		${PIANOBAR_DIR}/platform.c \
		${PIANOBAR_DIR}/credential.c \
		${PIANOBAR_DIR}/debug.c \
		${PIANOBAR_DIR}/player.c \
		${PIANOBAR_DIR}/settings.c \
		${PIANOBAR_DIR}/spectrum.c \
		${PIANOBAR_DIR}/station_browser.c \
		${PIANOBAR_DIR}/ui_act.c \
		${PIANOBAR_DIR}/ui.c \
		${PIANOBAR_DIR}/ui_renderer.c \
		${PIANOBAR_DIR}/ui_dispatch.c
ifeq (${WINDOWS},1)
	PIANOBAR_SRC+=${PIANOBAR_DIR}/terminal_win32.c \
		${PIANOBAR_DIR}/ui_renderer_curses_win32.c \
		${PIANOBAR_DIR}/ui_readline_win32.c
else
	PIANOBAR_SRC+=${PIANOBAR_DIR}/terminal.c \
		${PIANOBAR_DIR}/ui_renderer_curses.c \
		${PIANOBAR_DIR}/ui_readline.c
endif
PIANOBAR_OBJ:=${PIANOBAR_SRC:.c=.o}

LIBPIANO_DIR:=src/libpiano
LIBPIANO_SRC:=\
		${LIBPIANO_DIR}/crypt.c \
		${LIBPIANO_DIR}/piano.c \
		${LIBPIANO_DIR}/request.c \
		${LIBPIANO_DIR}/response.c \
		${LIBPIANO_DIR}/list.c
LIBPIANO_OBJ:=${LIBPIANO_SRC:.c=.o}
LIBPIANO_RELOBJ:=${LIBPIANO_SRC:.c=.lo}
LIBPIANO_INCLUDE:=${LIBPIANO_DIR}

LIBAV_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libavcodec libavformat libavutil libavfilter)
LIBAV_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libavcodec libavformat libavutil libavfilter)

LIBCURL_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libcurl)
LIBCURL_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libcurl)

LIBGCRYPT_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libgcrypt)
LIBGCRYPT_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libgcrypt)

LIBJSONC_CFLAGS:=$(shell $(PKG_CONFIG) --cflags json-c 2>/dev/null || $(PKG_CONFIG) --cflags json)
LIBJSONC_LDFLAGS:=$(shell $(PKG_CONFIG) --libs json-c 2>/dev/null || $(PKG_CONFIG) --libs json)

LIBAO_CFLAGS:=$(shell $(PKG_CONFIG) --cflags ao)
LIBAO_LDFLAGS:=$(shell $(PKG_CONFIG) --libs ao)

ifneq (${WINDOWS},1)
	NCURSESW_CFLAGS:=$(shell $(PKG_CONFIG) --cflags ncursesw)
	NCURSESW_LDFLAGS:=$(shell $(PKG_CONFIG) --libs ncursesw)
endif

ifeq (${HOST_OS},Darwin)
	CREDENTIAL_LDFLAGS:=-framework Security -framework CoreFoundation
else ifeq ($(shell $(PKG_CONFIG) --exists libsecret-1 && echo yes),yes)
	CREDENTIAL_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libsecret-1) -DHAVE_LIBSECRET
	CREDENTIAL_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libsecret-1)
endif

# combine all flags
ALL_CFLAGS:=${CFLAGS} -I ${LIBPIANO_INCLUDE} \
			${LIBAV_CFLAGS} ${LIBCURL_CFLAGS} \
			${LIBGCRYPT_CFLAGS} ${LIBJSONC_CFLAGS} \
			${LIBAO_CFLAGS} ${NCURSESW_CFLAGS} ${CREDENTIAL_CFLAGS}
ALL_LDFLAGS:=${LDFLAGS} -lpthread -lm \
			${LIBAV_LDFLAGS} ${LIBCURL_LDFLAGS} \
			${LIBGCRYPT_LDFLAGS} ${LIBJSONC_LDFLAGS} \
			${LIBAO_LDFLAGS} ${NCURSESW_LDFLAGS} ${CREDENTIAL_LDFLAGS}
ifeq (${WINDOWS},1)
	ALL_CFLAGS+=-D_WIN32_WINNT=0x0600
	ALL_LDFLAGS+=-lshell32 -lole32
endif

# Be verbose if V=1 (gnu autotools’ --disable-silent-rules)
SILENTCMD:=@
SILENTECHO:=@echo
ifeq (${V},1)
	SILENTCMD:=
	SILENTECHO:=@true
endif

ifeq (${HOST_OS},Darwin)
ifneq ($(strip ${MACOS_CODESIGN_IDENTITY}),)
define MACOS_CODESIGN
	${SILENTECHO} "  SIGN  $@"
	${SILENTCMD}codesign --force --sign "${MACOS_CODESIGN_IDENTITY}" \
			--identifier "${MACOS_CODESIGN_IDENTIFIER}" $@
endef
endif
endif

# build signalbox
ifeq (${DYNLINK},1)
${PROGRAM}: ${PIANOBAR_OBJ} libpiano.so.0
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -o $@ ${PIANOBAR_OBJ} -L. -lpiano ${ALL_LDFLAGS}
	$(MACOS_CODESIGN)
else
${PROGRAM}: ${PIANOBAR_OBJ} ${LIBPIANO_OBJ}
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -o $@ ${PIANOBAR_OBJ} ${LIBPIANO_OBJ} ${ALL_LDFLAGS}
	$(MACOS_CODESIGN)
endif

# build shared and static libpiano
libpiano.so.0: ${LIBPIANO_RELOBJ} ${LIBPIANO_OBJ}
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -shared -Wl,-soname,libpiano.so.0 -o libpiano.so.0.0.0 \
			${LIBPIANO_RELOBJ} ${ALL_LDFLAGS}
	${SILENTCMD}ln -fs libpiano.so.0.0.0 libpiano.so.0
	${SILENTCMD}ln -fs libpiano.so.0 libpiano.so
	${SILENTECHO} "    AR  libpiano.a"
	${SILENTCMD}${AR} rcs libpiano.a ${LIBPIANO_OBJ}


-include $(PIANOBAR_SRC:.c=.d)
-include $(LIBPIANO_SRC:.c=.d)

# build standard object files
%.o: %.c
	${SILENTECHO} "    CC  $<"
	${SILENTCMD}${CC} -c -o $@ ${ALL_CFLAGS} -MMD -MF $*.d -MP $<

# create position independent code (for shared libraries)
%.lo: %.c
	${SILENTECHO} "    CC  $< (PIC)"
	${SILENTCMD}${CC} -c -fPIC -o $@ ${ALL_CFLAGS} -MMD -MF $*.d -MP $<

clean:
	${SILENTECHO} " CLEAN"
	${SILENTCMD}${RM} ${PIANOBAR_OBJ} ${LIBPIANO_OBJ} \
			${LIBPIANO_RELOBJ} ${PROGRAM_BASE} ${PROGRAM_BASE}.exe spectrum-test spectrum-test.exe pianobar libpiano.so* \
			libpiano.a $(PIANOBAR_SRC:.c=.d) $(LIBPIANO_SRC:.c=.d)

all: ${PROGRAM}

spectrum-test: tests/spectrum_test.c src/spectrum.c src/spectrum.h src/platform.c src/platform.h
	${CC} -O2 -I src ${LIBAV_CFLAGS} -o $@$(EXEEXT) tests/spectrum_test.c src/spectrum.c src/platform.c -lpthread -lm $(if ${WINDOWS},-lshell32 -lole32)
	./$@$(EXEEXT)

ifeq (${DYNLINK},1)
install: ${PROGRAM} install-libpiano
else
install: ${PROGRAM}
endif
	install -d ${DESTDIR}${BINDIR}/
	install -m755 ${PROGRAM} ${DESTDIR}${BINDIR}/
	install -d ${DESTDIR}${MANDIR}/man1/
	install -m644 contrib/signalbox.1 ${DESTDIR}${MANDIR}/man1/

install-libpiano:
	install -d ${DESTDIR}${LIBDIR}/
	install -m644 libpiano.so.0.0.0 ${DESTDIR}${LIBDIR}/
	ln -fs libpiano.so.0.0.0 ${DESTDIR}${LIBDIR}/libpiano.so.0
	ln -fs libpiano.so.0 ${DESTDIR}${LIBDIR}/libpiano.so
	install -m644 libpiano.a ${DESTDIR}${LIBDIR}/
	install -d ${DESTDIR}${INCDIR}/
	install -m644 src/libpiano/piano.h ${DESTDIR}${INCDIR}/

uninstall:
	$(RM) ${DESTDIR}/${BINDIR}/${PROGRAM} \
	${DESTDIR}/${MANDIR}/man1/signalbox.1 \
	${DESTDIR}/${LIBDIR}/libpiano.so.0.0.0 \
	${DESTDIR}/${LIBDIR}/libpiano.so.0 \
	${DESTDIR}/${LIBDIR}/libpiano.so \
	${DESTDIR}/${LIBDIR}/libpiano.a \
	${DESTDIR}/${INCDIR}/piano.h

.PHONY: install install-libpiano uninstall test debug all spectrum-test
