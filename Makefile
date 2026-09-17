# makefile of pianobar

PKG_CONFIG?=pkg-config
PROGRAM_BASE:=signalbox
HOST_OS:=$(shell uname -s 2>/dev/null)
ifneq ($(filter Windows_NT MINGW% MSYS%,$(OS) $(HOST_OS)),)
	WINDOWS:=1
	EXEEXT:=.exe
endif
ifneq (${WINDOWS},1)
	CPPFLAGS+=-D_POSIX_C_SOURCE=200809L
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
		${PIANOBAR_DIR}/enrichment.c \
		${PIANOBAR_DIR}/lyrics_sync.c \
		${PIANOBAR_DIR}/enrichment_cache.c \
		${PIANOBAR_DIR}/album_art.c \
		${PIANOBAR_DIR}/art_renderer.c \
		${PIANOBAR_DIR}/tui_presentation.c \
		${PIANOBAR_DIR}/player.c \
		${PIANOBAR_DIR}/settings.c \
		${PIANOBAR_DIR}/settings_values.c \
		${PIANOBAR_DIR}/spectrum.c \
		${PIANOBAR_DIR}/station_browser.c \
		${PIANOBAR_DIR}/ui_act.c \
		${PIANOBAR_DIR}/ui.c \
		${PIANOBAR_DIR}/ui_renderer.c \
		${PIANOBAR_DIR}/ui_dispatch.c \
		${PIANOBAR_DIR}/ui_keymap.c
ifeq (${WINDOWS},1)
	PIANOBAR_SRC+=${PIANOBAR_DIR}/terminal_win32.c \
		${PIANOBAR_DIR}/terminal_input_win32.c \
		${PIANOBAR_DIR}/ui_renderer_curses.c \
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
LIBSWSCALE_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libswscale)
LIBSWSCALE_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libswscale)

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
else
# MSYS2 packages PDCursesMod's wide/UTF-8 ports as separate static archives.
# Both renderers use Signalbox's native console input; WinCon is the default
# while VT remains selectable for direct output-backend comparisons.
WINDOWS_CURSES_BACKEND?=wincon
ifeq ($(wildcard ${MINGW_PREFIX}/include/pdcurses.h),)
$(error Windows TUI requires PDCursesMod: pacman -S mingw-w64-ucrt-x86_64-pdcurses)
endif
ifeq (${WINDOWS_CURSES_BACKEND},wincon)
PDCURSESMOD_LIBRARY:=pdcurses_wincon
PDCURSESMOD_BACKEND_CFLAGS:=-DSIGNALBOX_PDCURSES_WINCON
PDCURSESMOD_BACKEND_LDFLAGS:=-lwinmm
else ifeq (${WINDOWS_CURSES_BACKEND},vt)
PDCURSESMOD_LIBRARY:=pdcurses_vt
PDCURSESMOD_BACKEND_CFLAGS:=-DSIGNALBOX_PDCURSES_VT
else
$(error WINDOWS_CURSES_BACKEND must be wincon or vt)
endif
ifeq ($(wildcard ${MINGW_PREFIX}/lib/lib${PDCURSESMOD_LIBRARY}.a),)
$(error Windows TUI requires ${MINGW_PREFIX}/lib/lib${PDCURSESMOD_LIBRARY}.a: pacman -S mingw-w64-ucrt-x86_64-pdcurses)
endif
PDCURSESMOD_CFLAGS?=
PDCURSESMOD_LDFLAGS?=-l${PDCURSESMOD_LIBRARY} ${PDCURSESMOD_BACKEND_LDFLAGS}
endif

ifeq (${HOST_OS},Darwin)
	CREDENTIAL_LDFLAGS:=-framework Security -framework CoreFoundation
else ifeq ($(shell $(PKG_CONFIG) --exists libsecret-1 && echo yes),yes)
	CREDENTIAL_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libsecret-1) -DHAVE_LIBSECRET
	CREDENTIAL_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libsecret-1)
endif

# combine all flags
ALL_CFLAGS:=${CFLAGS} -I ${LIBPIANO_INCLUDE} \
			${LIBAV_CFLAGS} ${LIBSWSCALE_CFLAGS} ${LIBCURL_CFLAGS} \
			${LIBGCRYPT_CFLAGS} ${LIBJSONC_CFLAGS} \
			${LIBAO_CFLAGS} ${NCURSESW_CFLAGS} ${PDCURSESMOD_CFLAGS} ${CREDENTIAL_CFLAGS}
ifeq (${WINDOWS},1)
	ALL_CFLAGS+=-DSIGNALBOX_PDCURSESMOD ${PDCURSESMOD_BACKEND_CFLAGS}
endif
ALL_LDFLAGS:=${LDFLAGS} -lpthread -lm \
			${LIBAV_LDFLAGS} ${LIBSWSCALE_LDFLAGS} ${LIBCURL_LDFLAGS} \
			${LIBGCRYPT_LDFLAGS} ${LIBJSONC_LDFLAGS} \
			${LIBAO_LDFLAGS} ${NCURSESW_LDFLAGS} ${PDCURSESMOD_LDFLAGS} ${CREDENTIAL_LDFLAGS}
ifeq (${WINDOWS},1)
	ALL_CFLAGS+=-D_WIN32_WINNT=0x0600
	ALL_LDFLAGS+=-lshell32 -lole32 -luuid
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
	${SILENTCMD}${CC} ${CPPFLAGS} -c -o $@ ${ALL_CFLAGS} -MMD -MF $*.d -MP $<

# create position independent code (for shared libraries)
%.lo: %.c
	${SILENTECHO} "    CC  $< (PIC)"
	${SILENTCMD}${CC} ${CPPFLAGS} -c -fPIC -o $@ ${ALL_CFLAGS} -MMD -MF $*.d -MP $<

TEST_TARGETS:=spectrum-test enrichment-test enrichment-cache-test album-art-test \
	art-renderer-test lyrics-sync-test tui-presentation-test \
	playlist-prefetch-test station-browser-test settings-values-test

clean:
	${SILENTECHO} " CLEAN"
	${SILENTCMD}${RM} ${PIANOBAR_OBJ} ${LIBPIANO_OBJ} \
		${LIBPIANO_RELOBJ} ${PROGRAM_BASE} ${PROGRAM_BASE}.exe spectrum-test spectrum-test.exe enrichment-test enrichment-test.exe enrichment-cache-test enrichment-cache-test.exe album-art-test album-art-test.exe playlist-prefetch-test playlist-prefetch-test.exe pianobar libpiano.so* \
		libpiano.a art-renderer-test art-renderer-test.exe tui-presentation-test tui-presentation-test.exe lyrics-sync-test lyrics-sync-test.exe station-browser-test station-browser-test.exe settings-values-test settings-values-test.exe $(PIANOBAR_SRC:.c=.d) $(LIBPIANO_SRC:.c=.d)

all: ${PROGRAM}

test: ${TEST_TARGETS}

settings-values-test: tests/settings_values_test.c src/settings_values.c src/settings_values.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src -o $@$(EXEEXT) tests/settings_values_test.c src/settings_values.c
	./$@$(EXEEXT)

station-browser-test: tests/station_browser_test.c src/station_browser.c src/station_browser.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src ${ALL_CFLAGS} -UNDEBUG -o $@$(EXEEXT) tests/station_browser_test.c src/station_browser.c
	./$@$(EXEEXT)

spectrum-test: tests/spectrum_test.c src/spectrum.c src/spectrum.h src/platform.c src/platform.h
	${CC} ${CPPFLAGS} -O2 -I src ${LIBAV_CFLAGS} -o $@$(EXEEXT) tests/spectrum_test.c src/spectrum.c src/platform.c -lpthread -lm $(if ${WINDOWS},-lshell32 -lole32 -luuid)
	./$@$(EXEEXT)

enrichment-test: tests/enrichment_test.c src/enrichment.c src/enrichment.h src/enrichment_cache.c src/album_art.c src/debug.c src/debug.h src/modal_state.h src/mouse_state.h src/platform.c src/platform.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src ${LIBAV_CFLAGS} ${LIBCURL_CFLAGS} ${LIBJSONC_CFLAGS} -o $@$(EXEEXT) tests/enrichment_test.c src/enrichment.c src/enrichment_cache.c src/album_art.c src/debug.c src/platform.c -lpthread ${LIBCURL_LDFLAGS} ${LIBJSONC_LDFLAGS} $(if ${WINDOWS},-lshell32 -lole32 -luuid)
	./$@$(EXEEXT)

enrichment-cache-test: tests/enrichment_cache_test.c src/enrichment_cache.c src/enrichment_cache.h src/platform.c src/platform.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src ${LIBJSONC_CFLAGS} -o $@$(EXEEXT) tests/enrichment_cache_test.c src/enrichment_cache.c src/platform.c ${LIBJSONC_LDFLAGS} $(if ${WINDOWS},-lshell32 -lole32 -luuid)
	./$@$(EXEEXT)

album-art-test: tests/album_art_test.c src/album_art.c src/album_art.h src/platform.c src/platform.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src ${LIBCURL_CFLAGS} ${LIBJSONC_CFLAGS} -o $@$(EXEEXT) tests/album_art_test.c src/album_art.c src/platform.c ${LIBCURL_LDFLAGS} ${LIBJSONC_LDFLAGS} $(if ${WINDOWS},-lshell32 -lole32 -luuid)
	./$@$(EXEEXT)

art-renderer-test: tests/art_renderer_test.c src/art_renderer.c src/art_renderer.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src ${LIBAV_CFLAGS} ${LIBSWSCALE_CFLAGS} -o $@$(EXEEXT) tests/art_renderer_test.c src/art_renderer.c ${LIBAV_LDFLAGS} ${LIBSWSCALE_LDFLAGS}
	./$@$(EXEEXT)

tui-presentation-test: tests/tui_presentation_test.c src/tui_presentation.c src/tui_presentation.h src/ui_keymap.c src/ui_dispatch.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src -I ${LIBPIANO_INCLUDE} ${LIBAV_CFLAGS} ${LIBCURL_CFLAGS} ${LIBGCRYPT_CFLAGS} ${LIBJSONC_CFLAGS} ${LIBAO_CFLAGS} -o $@$(EXEEXT) tests/tui_presentation_test.c src/tui_presentation.c src/ui_keymap.c
	./$@$(EXEEXT)

lyrics-sync-test: tests/lyrics_sync_test.c src/lyrics_sync.c src/lyrics_sync.h
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src -o $@$(EXEEXT) tests/lyrics_sync_test.c src/lyrics_sync.c
	./$@$(EXEEXT)

playlist-prefetch-test: tests/playlist_prefetch_test.c src/playlist_prefetch.h ${LIBPIANO_SRC}
	${CC} ${CPPFLAGS} -std=c99 -O2 -I src -I ${LIBPIANO_INCLUDE} ${LIBAV_CFLAGS} ${LIBCURL_CFLAGS} ${LIBGCRYPT_CFLAGS} ${LIBJSONC_CFLAGS} ${LIBAO_CFLAGS} ${NCURSESW_CFLAGS} -o $@$(EXEEXT) tests/playlist_prefetch_test.c ${LIBPIANO_SRC} ${ALL_LDFLAGS}
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

.PHONY: clean install install-libpiano uninstall test debug all ${TEST_TARGETS}
