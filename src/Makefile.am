AUTOMAKE_OPTIONS = -Wall foreign
ACLOCAL_AMFLAGS = ${ALOCAL_FLAGS}

AM_CFLAGS = -Wall -Werror -g $(json_c_CFLAGS) $(json_c_LIBS) \
	-llustreapi -lpthread -lyaml

bin_PROGRAMS = lcrpd
lcrpd_SOURCES = lcrp_changelog.c debug.c debug.h lcrpd.h lcrpd.c

C_FILES = $(wildcard *.c *.h)
C_CHECKS = $(C_FILES:%=%.c_checked)
CHECKS = $(C_CHECKS)

%.c_checked: % ../lcrp_c_check.pl
	../lcrp_c_check.pl -f $<
	touch $@

check_clean-local:
	rm -f $(CHECKS)

check-local: $(CHECKS)

all: all-am $(CHECKS)
