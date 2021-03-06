-include .config

INSTALL ?= install

# Installation paths

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS += -MMD -Wall -DUSE_PORTAUDIO
CFLAGS += -I/usr/local/Cellar/ortp/4.3.2/libexec/include/

#LDLIBS_ASOUND ?= -lasound
LDLIBS_OPUS ?= -lopus
LDLIBS_ORTP ?= -lortp 

# the following line is only for linking ortp dependency on Mac
# and can be commented out on linux
LDLIBS_ORTP += /usr/local/Cellar/ortp/4.3.2/libexec/lib/libbctoolbox.a

LDLIBS_PORTAUDIO ?= -lportaudio

LDLIBS += $(LDLIBS_ASOUND) $(LDLIBS_OPUS) $(LDLIBS_ORTP) $(LDLIBS_PORTAUDIO)

.PHONY:		all install dist clean

all:		rx tx detect protoring

protoring: protoring.o pa_ringbuffer.o

detect: detect.o

rx:		rx.o device.o sched.o payload_type_opus.o

tx:		tx.o device.o sched.o

install:	rx tx
		$(INSTALL) -d $(DESTDIR)$(BINDIR)
		$(INSTALL) rx tx $(DESTDIR)$(BINDIR)

dist:
		mkdir -p dist
		V=$$(git describe) && \
		git archive --prefix="trx-$$V/" HEAD | \
			gzip > "dist/trx-$$V.tar.gz"

clean:
		rm -f *.o *.d tx rx detect protoring

-include *.d
