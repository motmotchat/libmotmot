#
# Paxos test binary Makefile.
#

MOTMOT_ROOT = ../..

all: motmot
	@mkdir -p sock

motmot:
	$(MAKE) -C $(MOTMOT_ROOT) motmot

clean: unsock
	$(MAKE) -C $(MOTMOT_ROOT) clean-paxos

unsock:
	-rm -rf sock/*
