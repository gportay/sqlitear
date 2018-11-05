/*
 *  Copyright (C) 2018 Gaël PORTAY
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
const char VERSION[] = __DATE__ " " __TIME__;
#endif /* HAVE_CONFIG_H */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sqlite3.h>

#define __sqlite3_perror(s, db) do { \
	fprintf(stderr, "%s: %s\n", s, sqlite3_errmsg(db)); \
} while (0)

struct options {
	int argc;
	char * const *argv;
	const char *file;
	int extract;
	int list;
};

int printfile(sqlite3 *db, const char *file)
{
	(void)db;
	printf("%s\n", file);
	return 0;
}

int list(sqlite3 *db, int (*callback)(sqlite3 *, const char *)) {
	int ret = -1;

	if (!callback)
		goto exit;

	for (;;) {
		sqlite3_stmt *stmt;
		const char *sql;

		sql = "SELECT file FROM blobs";
		if (sqlite3_prepare(db, sql, -1, &stmt, 0)) {
			__sqlite3_perror("sqlite3_prepare", db);
			goto exit;
		}

		for (;;) {
			int rc = sqlite3_step(stmt);
			if (rc == SQLITE_DONE) {
				break;
			} else if (rc == SQLITE_ROW) {
				if (callback(db,
				     (const char*)sqlite3_column_text(stmt, 0)))
					goto exit;

				continue;
			}

			__sqlite3_perror("sqlite3_step", db);
			goto exit;
		}

		if (sqlite3_finalize(stmt) == SQLITE_SCHEMA) {
			__sqlite3_perror("sqlite3_step", db);
			continue;
		}

		break;
	}

	ret = 0;

exit:
	return ret;
}

int extract(sqlite3 *db, const char *file)
{
	int fd = -1, ret = -1;

	for (;;) {
		sqlite3_stmt *stmt;
		const char *sql;
		ssize_t size;

		sql = "SELECT content FROM blobs WHERE file = ?";
		if (sqlite3_prepare(db, sql, -1, &stmt, 0)) {
			__sqlite3_perror("sqlite3_prepare", db);
			goto exit;
		}

		if (sqlite3_bind_text(stmt, 1, file, -1, SQLITE_STATIC)) {
			__sqlite3_perror("sqlite3_bind_text", db);
			goto exit;
		}

		if (sqlite3_step(stmt) != SQLITE_ROW) {
			__sqlite3_perror("sqlite3_step", db);
			goto exit;
		}

		fd = open(file, O_CREAT | O_WRONLY);
		if (fd == -1) {
			perror("open");
			goto exit;
		}

		size = write(fd, sqlite3_column_blob(stmt, 0),
			     sqlite3_column_bytes(stmt, 0));
		if (size == -1) {
			perror("write");
			goto exit;
		} else if (size < sqlite3_column_bytes(stmt, 0)) {
			fprintf(stderr, "write: Truncated\n");
		}

		if (sqlite3_finalize(stmt) == SQLITE_SCHEMA) {
			__sqlite3_perror("sqlite3_step", db);
			continue;
		}

		break;
	}

	ret = 0;

exit:
	if (fd != -1) {
		if (close(fd))
			perror("close");
		fd = -1;
	}

	return ret;
}

int archive(sqlite3 *db, const char *file) {
	int fd = -1, ret = -1;

	for (;;) {
		unsigned char blob[BUFSIZ];
		sqlite3_stmt *stmt;
		const char *sql;
		ssize_t size;

		fd = open(file, O_RDONLY);
		if (fd == -1) {
			perror("open");
			goto exit;
		}

		size = read(fd, blob, sizeof(blob));
		if (size == -1) {
			perror("read");
			goto exit;
		}

		sql = "INSERT OR REPLACE INTO blobs(file, content) VALUES(?, ?)";
		if (sqlite3_prepare(db, sql, -1, &stmt, 0) != SQLITE_OK) {
			__sqlite3_perror("sqlite3_prepare", db);
			goto exit;
		}

		if (sqlite3_bind_text(stmt, 1, file, -1, SQLITE_STATIC)) {
			__sqlite3_perror("sqlite3_bind_text", db);
			goto exit;
		}

		if (sqlite3_bind_blob(stmt, 2, blob, size, SQLITE_STATIC)) {
			__sqlite3_perror("sqlite3_bind_blob", db);
			goto exit;
		}

		if (sqlite3_step(stmt) != SQLITE_DONE) {
			__sqlite3_perror("sqlite3_step", db);
			goto exit;
		}

		if (sqlite3_finalize(stmt) == SQLITE_SCHEMA) {
			__sqlite3_perror("sqlite3_step", db);
			continue;
		}

		break;
	}

	ret = 0;

exit:
	if (fd != -1) {
		if (close(fd))
			perror("close");
		fd = -1;
	}

	return ret;
}

static inline const char *applet(const char *arg0)
{
	char *s = strrchr(arg0, '/');
	if (!s)
		return arg0;

	return s+1;
}

void usage(FILE * f, char * const arg0)
{
	fprintf(f, "Usage: %s [OPTIONS] -f DATABASE [FILE...]\n"
		   "\n"
		   "Saves many files together into a single database, and can "
		   "restore individual files\nfrom the database.\n"
		   "\n"
		   "Options:\n"
		   " -f or --file FILE     Set the path to database.\n"
		   " -x or --extract       Extract file from a database.\n"
		   " -t or --list          List the contents of a database.\n"
		   " -h or --help          Display this message.\n"
		   " -V or --version       Display the version.\n"
		   "", applet(arg0));
}

int parse_arguments(struct options *opts, int argc, char * const argv[])
{
	static const struct option long_options[] = {
		{ "file",    required_argument, NULL, 'f' },
		{ "extract", no_argument,       NULL, 'x' },
		{ "list",    no_argument,       NULL, 't' },
		{ "version", no_argument,       NULL, 'V' },
		{ "help",    no_argument,       NULL, 'h' },
		{ NULL,      no_argument,       NULL, 0   }
	};

	opterr = 0;
	for (;;) {
		int index;
		int c = getopt_long(argc, argv, "f:xtVh", long_options, &index);
		if (c == -1) {
			break;
		}

		switch (c) {
		case 'f':
			opts->file = optarg;
			break;

		case 'x':
			opts->extract = 1;
			break;

		case 't':
			opts->list = 1;
			break;

		case 'V':
			printf("%s\n", VERSION);
			exit(EXIT_SUCCESS);
			break;

		case 'h':
			usage(stdout, argv[0]);
			exit(EXIT_SUCCESS);
			break;

		default:
		case '?':
			return -1;
		}
	}

	opts->argc = argc;
	opts->argv = argv;
	return optind;
}

int main(int argc, char * const argv[])
{
	int exist, ret = EXIT_FAILURE;
	static struct options options;
	struct stat buf;
	sqlite3 *db;

	int argi = parse_arguments(&options, argc, argv);
	if (argi < 0) {
		fprintf(stderr, "Error: %s: Invalid option!\n", argv[optind-1]);
		exit(EXIT_FAILURE);
	} else if (!options.list && !options.extract && (argc - argi < 1)) {
		usage(stdout, argv[0]);
		fprintf(stderr, "Error: Too few arguments!\n");
		exit(EXIT_FAILURE);
	} else if (!options.file || !*options.file) {
		usage(stdout, argv[0]);
		fprintf(stderr, "Error: No such database! Use -f DATABASE.\n");
		exit(EXIT_FAILURE);
	}

	exist = stat(options.file, &buf) == 0;

	if (sqlite3_open(options.file, &db)) {
		__sqlite3_perror("sqlite3_open_v2", db);
		goto exit;
	}

	if (!exist) {
		const char *sql;

		sql = "CREATE TABLE blobs(file TEXT PRIMARY KEY, content BLOB)";
		if (sqlite3_exec(db, sql, 0, 0, 0)) {
			__sqlite3_perror("sqlite3_exec", db);
			goto exit;
		}
	}

	if (options.extract) {
		int i;

		if (argc - argi == 0) {
			ret = list(db, extract);
			goto exit;
		}
		
		for (i = optind; i < argc; i++)
			if (extract(db, argv[i]))
				goto exit;

		ret = 0;
	} else if (options.list) {
		ret = list(db, printfile);
	} else {
		int i;

		for (i = optind; i < argc; i++)
			if (archive(db, argv[i]))
				goto exit;

		ret = 0;
	}
exit:
	sqlite3_close(db);
	return ret;
}
