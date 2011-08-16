/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Abhinav Upadhyay.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef APROPOS_UTILS_H
#define APROPOS_UTILS_H

#include <zlib.h> 
#include "sqlite3.h"

#define DBPATH "./apropos.db"
#define SECMAX 9

/* Flags for opening the database */
#define DB_READONLY SQLITE_OPEN_READONLY
#define DB_WRITE SQLITE_OPEN_READWRITE
#define DB_CREATE SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE


enum man_sec {
	MANSEC_NAME = 0,
	MANSEC_SYNOPSIS,
	MANSEC_LIBRARY,
	MANSEC_ERRORS,
	MANSEC_FILES,
	MANSEC_RETURN_VALUES,
	MANSEC_EXIT_STATUS,
	MANSEC_DESCRIPTION,
	MANSEC_ENVIRONMENT,
	MANSEC_DIAGNOSTICS,
	MANSEC_NONE
};

typedef struct query_args {
	const char *search_str;		// user query
	const char **sec_nums;		// Section in which to do the search
	const char *nrec;			// number of records to fetch
	int (*callback) (void *, int, char **, char **);	// The callback function
	void *callback_data;	// data to pass to the callback function
	char **errmsg;		// buffer for storing the error msg
} query_args;

char *lower(char *);
void concat(char **, const char *, int);
sqlite3 *init_db(int);
void close_db(sqlite3 *);
int run_query(sqlite3 *, const char **, query_args *);
int run_query_html(sqlite3 *, query_args *);
int run_query_pager(sqlite3 *, query_args *);
#endif 
