#
#  Copyright (C) 2018 Gaël PORTAY
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

override CFLAGS += $(shell pkg-config sqlite3 --cflags)
override LDLIBS += $(shell pkg-config sqlite3 --libs)

.PHONY: all
all: override CFLAGS += -g -Wall -Wextra -Werror
all: sqlitear

.PHONY: clean
clean:
	rm -f sqlitear database.sql

