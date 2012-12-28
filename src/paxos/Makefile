#
# Paxos test binary Makefile.
#

all: motmot
	@mkdir -p sock

motmot:
	$(MAKE) -C ../.. motmot

clean: unsock
	$(MAKE) -C ../.. clean-paxos

unsock:
	-rm -rf sock/*
