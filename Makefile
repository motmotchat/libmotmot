#
# Toplevel make for Motmot.
#
# The source tree is mirrored in the build target obj/.  Binaries are created
# in the same directories as their mains.  Dependencies are stored in the .deps
# toplevel directory, which also mirrors the source tree.
#

CC = gcc
CFLAGS = -ggdb3 -Wall -Werror -O2 -DTRILL_USE_GNUTLS=1
CFLAGS += `pkg-config --cflags glib-2.0 gio-2.0 gnutls`
CFLAGS += -I./include -I./ext -I./src -I./src/paxos -I./src/network

ifdef DEBUG
	CFLAGS += -DDEBUG
endif

LDFLAGS = `pkg-config --libs glib-2.0 gio-2.0 gnutls` -lmsgpack

SRCDIR = src
OBJDIR = obj
EXTDIR = ext
DEPDIR = .deps

SOURCES := $(shell find $(SRCDIR) $(EXTDIR) -name '*.c')
HEADERS := $(shell find $(SRCDIR) $(EXTDIR) -name '*.h')

MAINS = \
	$(SRCDIR)/paxos/main.c \
	$(SRCDIR)/network/trill/main.c \
	$(SRCDIR)/network/plume/main.c

OBJS = $(addprefix $(OBJDIR)/,$(patsubst %.c,%.o,$(filter-out $(MAINS),$(SOURCES))))
DIRS = $(filter-out ./,$(sort $(dir $(SOURCES))))

# Temporary object sets for test binary dev.
COMMON_OBJS = $(filter $(OBJDIR)/$(SRCDIR)/common/%,$(OBJS))
PAXOS_OBJS = $(filter-out $(OBJDIR)/$(SRCDIR)/trill/%,$(OBJS))
TRILL_OBJS = \
	$(COMMON_OBJS) \
	$(filter $(OBJDIR)/$(SRCDIR)/network/trill/%,$(OBJS))
PLUME_OBJS = \
	$(COMMON_OBJS) \
	$(OBJDIR)/$(SRCDIR)/config.o \
	$(filter $(OBJDIR)/$(SRCDIR)/network/plume/%,$(OBJS))

all: $(OBJS) motmot trill plume tags

define mkbin
$(CC) $(LDFLAGS) $^ -o $(subst $(OBJDIR)/,,$(<D))/$@
endef

motmot: $(OBJDIR)/$(SRCDIR)/paxos/main.o $(PAXOS_OBJS); $(mkbin)
trill: $(OBJDIR)/$(SRCDIR)/network/trill/main.o $(TRILL_OBJS); $(mkbin)
plume: $(OBJDIR)/$(SRCDIR)/network/plume/main.o $(PLUME_OBJS); $(mkbin)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	@mkdir -p $(DEPDIR)/$(<D)
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)/$(<:.c=.d) -c $< -o $@

tags: $(SOURCES) $(HEADERS)
	ctags -R

clean: clean-paxos clean-trill
	-rm -rf $(DEPDIR) $(OBJDIR)

clean-paxos:
	-rm -rf $(PAXOS_OBJS) $(SRCDIR)/paxos/motmot

clean-trill:
	-rm -rf $(TRILL_OBJS) $(SRCDIR)/network/trill/trill

clean-plume:
	-rm -rf $(PLUME_OBJS) $(SRCDIR)/network/plume/plume

-include $(addprefix $(DEPDIR),$(SOURCES:.c=.d))
