#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "mdoc.h"
#include "sqlite3.h"

#define MAXLINE 1024	//buffer size for fgets
#define DBPATH "./apropos.db"

typedef struct {
int docid;
const char *term;
double tf;
} term_frequency;

typedef struct {
const char *term;
double idf;
int status;
} inverse_document_frequency;

static int build_term_weights(void);
static int compute_term_weight(sqlite3 *, const char *);
static int store_tf(sqlite3 *, term_frequency *);
static int store_idf(sqlite3 *, inverse_document_frequency *);
static int check_md5(const char *);
static void cleanup(void);
static int concat(char **, const char *);
static int create_db(void);
static int get_ndoc(void);
static void get_section(const char *);
static int insert_into_db(void);
static int is_stopword(const char *);
static	void pmdoc(const char *);
static void pmdoc_node(const struct mdoc_node *);
static void pmdoc_Nm(const struct mdoc_node *);
static void pmdoc_Nd(const struct mdoc_node *);
static void pmdoc_Sh(const struct mdoc_node *);
static void traversedir(const char *);
static int term_exists(sqlite3 *db, const char *);

static char *name = NULL;	// for storing the name of the man page
static char *name_desc = NULL; // for storing the one line description (.Nd)
static char *desc = NULL; // for storing the DESCRIPTION section
static char *md5_hash = NULL;
static char *section = NULL;
static struct mparse *mp = NULL;



typedef	void (*pmdoc_nf)(const struct mdoc_node *n);
static	const pmdoc_nf mdocs[MDOC_MAX] = {
	NULL, /* Ap */
	NULL, /* Dd */
	NULL, /* Dt */
	NULL, /* Os */
	pmdoc_Sh, /* Sh */ 
	NULL, /* Ss */ 
	NULL, /* Pp */ 
	NULL, /* D1 */
	NULL, /* Dl */
	NULL, /* Bd */
	NULL, /* Ed */
	NULL, /* Bl */ 
	NULL, /* El */
	NULL, /* It */
	NULL, /* Ad */ 
	NULL, /* An */ 
	NULL, /* Ar */
	NULL, /* Cd */ 
	NULL, /* Cm */
	NULL, /* Dv */ 
	NULL, /* Er */ 
	NULL, /* Ev */ 
	NULL, /* Ex */ 
	NULL, /* Fa */ 
	NULL, /* Fd */
	NULL, /* Fl */
	NULL, /* Fn */ 
	NULL, /* Ft */ 
	NULL, /* Ic */ 
	NULL, /* In */ 
	NULL, /* Li */
	pmdoc_Nd, /* Nd */
	pmdoc_Nm, /* Nm */
	NULL, /* Op */
	NULL, /* Ot */
	NULL, /* Pa */
	NULL, /* Rv */
	NULL, /* St */ 
	NULL, /* Va */
	NULL, /* Vt */ 
	NULL, /* Xr */ 
	NULL, /* %A */
	NULL, /* %B */
	NULL, /* %D */
	NULL, /* %I */
	NULL, /* %J */
	NULL, /* %N */
	NULL, /* %O */
	NULL, /* %P */
	NULL, /* %R */
	NULL, /* %T */
	NULL, /* %V */
	NULL, /* Ac */
	NULL, /* Ao */
	NULL, /* Aq */
	NULL, /* At */ 
	NULL, /* Bc */
	NULL, /* Bf */
	NULL, /* Bo */
	NULL, /* Bq */
	NULL, /* Bsx */
	NULL, /* Bx */
	NULL, /* Db */
	NULL, /* Dc */
	NULL, /* Do */
	NULL, /* Dq */
	NULL, /* Ec */
	NULL, /* Ef */ 
	NULL, /* Em */ 
	NULL, /* Eo */
	NULL, /* Fx */
	NULL, /* Ms */ 
	NULL, /* No */
	NULL, /* Ns */
	NULL, /* Nx */
	NULL, /* Ox */
	NULL, /* Pc */
	NULL, /* Pf */
	NULL, /* Po */
	NULL, /* Pq */
	NULL, /* Qc */
	NULL, /* Ql */
	NULL, /* Qo */
	NULL, /* Qq */
	NULL, /* Re */
	NULL, /* Rs */
	NULL, /* Sc */
	NULL, /* So */
	NULL, /* Sq */
	NULL, /* Sm */ 
	NULL, /* Sx */
	NULL, /* Sy */
	NULL, /* Tn */
	NULL, /* Ux */
	NULL, /* Xc */
	NULL, /* Xo */
	NULL, /* Fo */ 
	NULL, /* Fc */ 
	NULL, /* Oo */
	NULL, /* Oc */
	NULL, /* Bk */
	NULL, /* Ek */
	NULL, /* Bt */
	NULL, /* Hf */
	NULL, /* Fr */
	NULL, /* Ud */
	NULL, /* Lb */
	NULL, /* Lp */ 
	NULL, /* Lk */ 
	NULL, /* Mt */ 
	NULL, /* Brq */ 
	NULL, /* Bro */ 
	NULL, /* Brc */ 
	NULL, /* %C */
	NULL, /* Es */
	NULL, /* En */
	NULL, /* Dx */
	NULL, /* %Q */
	NULL, /* br */
	NULL, /* sp */
	NULL, /* %U */
	NULL, /* Ta */
};

int
main(int argc, char *argv[])
{
	FILE *file;
	char line[MAXLINE];
	mp = mparse_alloc(MPARSE_AUTO, MANDOCLEVEL_FATAL, NULL, NULL);
	
	/* Build the databases */
	if (create_db() < 0) 
		errx(EXIT_FAILURE, "Unable to create database");
			
	/* call man -p to get the list of man page dirs */
	if ((file = popen("man -p", "r")) == NULL)
		err(EXIT_FAILURE, "fopen failed");
	
	while (fgets(line, MAXLINE, file) != NULL) {
		/* remove the new line character from the string */
		line[strlen(line) - 1] = '\0';
		/* Traverse the man page directories and parse the pages */
		traversedir(line);
	}
	
	if (pclose(file) == -1)
		errx(EXIT_FAILURE, "pclose error");
	mparse_free(mp);

	/* Now, calculate the weights of each unique term in the index */
	if (build_term_weights() < 0)
		fprintf(stderr, "Could not compute the term weights. Please run makemandb again\n");
		
	cleanup();
	return 0;
}

/*
* traversedir --
*  traverses the given directory recursively and passes all the files in the
*  way to the parser.
*/
static void
traversedir(const char *file)
{
	struct stat sb;
	struct dirent *dirp;
	DIR *dp;
	char *buf;
	
	if (stat(file, &sb) < 0) {
		fprintf(stderr, "stat failed: %s", file);
		return;
	}
	
	/* if it is a regular file, pass it to the parser */
	if (S_ISREG(sb.st_mode)) {
		/* Avoid hardlinks to prevent duplicate entries in the database */
		if (check_md5(file) != 0) {
			cleanup();
			return;
		}
		
		get_section(file);
		
		printf("parsing %s\n", file);
		pmdoc(file);
		if (insert_into_db() < 0)
			fprintf(stderr, "Error indexing: %s\n", file);
		return;
	}
	
	/* if it is a directory`, traverse it recursively */
	else if (S_ISDIR(sb.st_mode)) {
		if ((dp = opendir(file)) == NULL) {
			fprintf(stderr, "opendir error: %s\n", file);
			return;
		}
		
		while ((dirp = readdir(dp)) != NULL) {
			/* Avoid . and .. entries in a directory */
			if (strncmp(dirp->d_name, ".", 1)) {
				if ((asprintf(&buf, "%s/%s", file, dirp->d_name) == -1)) {
					closedir(dp);
					if (errno == ENOMEM)
						fprintf(stderr, "ENOMEM\n");
					continue;
				}
				traversedir(buf);
				free(buf);
			}
		}
	
		closedir(dp);
	}
}		

/*
* parsemanpage --
*  parses the man page using libmandoc
*/
static void
pmdoc(const char *file)
{
	struct mdoc	*mdoc; /* resulting mdoc */
	mparse_reset(mp);

	if (mparse_readfd(mp, -1, file) >= MANDOCLEVEL_FATAL) {
		fprintf(stderr, "%s: Parse failure\n", file);
		return;
	}

	mparse_result(mp, &mdoc, NULL);
	if (mdoc == NULL)
		return;

	pmdoc_node(mdoc_node(mdoc));
}

static void
pmdoc_node(const struct mdoc_node *n)
{
	
	if (n == NULL)
		return;

	switch (n->type) {
	case (MDOC_HEAD):
		/* FALLTHROUGH */
	case (MDOC_BODY):
		/* FALLTHROUGH */
	case (MDOC_TAIL):
		/* FALLTHROUGH */
	case (MDOC_BLOCK):
		/* FALLTHROUGH */
	case (MDOC_ELEM):
		if (mdocs[n->tok] == NULL)
			break;

		(*mdocs[n->tok])(n);
		
	default:
		break;
	}

	pmdoc_node(n->child);
	pmdoc_node(n->next);
}

/*
* pmdoc_Nm --
*  Extracts the Name of the manual page from the .Nm macro
*/
static void
pmdoc_Nm(const struct mdoc_node *n)
{
	if (n->sec == SEC_NAME && n->child->type == MDOC_TEXT) {
		if ((name = strdup(n->child->string)) == NULL) {
			fprintf(stderr, "Memory allocation error");
			return;
		}
	}
	
	/* on encountering a .Nm macro in the DESCRIPTION section, copy the cached 
	* value of name at the end of desc
	*/
	else if (n->sec == SEC_DESCRIPTION && name != NULL) {
		if (desc == NULL)
			desc = strdup(name);
		else
			concat(&desc, name);
		}
}

/*
* pmdoc_Nd --
*  Extracts the one line description of the man page from the .Nd macro
*/
static void
pmdoc_Nd(const struct mdoc_node *n)
{
	for (n = n->child; n; n = n->next) {
		if (n->type != MDOC_TEXT)
			continue;

		if (name_desc == NULL) 
			name_desc = strdup(n->string);
		else 
			concat(&name_desc, n->string);
	}
}

/*
* pmdoc_Sh --
*  Extracts the complete DESCRIPTION section of the man page
*/
static void
pmdoc_Sh(const struct mdoc_node *n)
{
	if (n->sec == SEC_DESCRIPTION) {
		for(n = n->child; n; n = n->next) {
			if (n->type == MDOC_TEXT) {
				if (desc == NULL)
					desc = strdup(n->string);
				else {
					if (concat(&desc, n->string) < 0)
						return;
				}
			}
			else { 
				/* On encountering a .Nm macro, substitute it with it's previously
				* cached value of the argument
				*/
				if (mdocs[n->tok] == pmdoc_Nm && name != NULL)
					(*mdocs[n->tok])(n);
				/* otherwise call pmdoc_Sh again to handle the nested macros */
				else
					pmdoc_Sh(n);
			}
		}
	}
}

static void
get_section(const char *file)
{
	char *c = rindex(file, '.');
	section = strdup(++c);
}

/* cleanup --
*   cleans up the global buffers
*/
static void
cleanup(void)
{
	if (name)
		free(name);
	if (name_desc)
		free(name_desc);
	if (desc)
		free(desc);
	if (md5_hash)
		free(md5_hash);
	if (section)
		free(section);
	
	name = name_desc = desc = md5_hash = section = NULL;
}

/* insert_into_db --
*   Inserts the parsed data of the man page in the Sqlite databse.
*   If any of the values is NULL, then we cleanup and return -1 indicating an 
*   error. Otherwise, store the data in the database and return 0
*/
static int
insert_into_db(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	
	if (name == NULL || name_desc == NULL || desc == NULL || md5_hash == NULL 
		|| section == NULL) {
		cleanup();
		return -1;
	}
	else {

		sqlite3_initialize();
		rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE | 
				             SQLITE_OPEN_CREATE, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		sqlite3_extended_result_codes(db, 1);

/*------------------------ Populate the mandb table------------------------------ */
		sqlstr = "insert into mandb values (:section, :name, :name_desc, :desc)";
		rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}

		idx = sqlite3_bind_parameter_index(stmt, ":name");
		rc = sqlite3_bind_text(stmt, idx, name, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		idx = sqlite3_bind_parameter_index(stmt, ":section");
		rc = sqlite3_bind_text(stmt, idx, section, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}

		idx = sqlite3_bind_parameter_index(stmt, ":name_desc");
		rc = sqlite3_bind_text(stmt, idx, name_desc, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
	
		idx = sqlite3_bind_parameter_index(stmt, ":desc");
		rc = sqlite3_bind_text(stmt, idx, desc, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		sqlite3_finalize(stmt);
/*------------------------ Populate the mandb_md5 table-----------------------*/
		sqlstr = "insert into mandb_md5 values (:md5_hash)";
		rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
		rc = sqlite3_bind_text(stmt, idx, md5_hash, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
	
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		cleanup();
		return 0;
	}

}

static int
create_db(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	struct stat sb;
	
	if (stat(DBPATH, &sb) == 0 && S_ISREG(sb.st_mode)) {
		if (remove(DBPATH) < 0)
			return -1;
	}
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE | 
			                 SQLITE_OPEN_CREATE, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
/*------------------------ Build the mandb table------------------------------ */
	sqlstr = "create virtual table mandb using fts4(section, name, \
	name_desc, desc, tokenize=porter)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	sqlite3_finalize(stmt);

/*------------------------ Build the mandb_aux table------------------------------ */	
	sqlstr = "create virtual table mandb_aux using fts4aux(mandb)";

	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}

	sqlite3_finalize(stmt);

/*------------------------ Build the mandb_md5 table------------------------------ */	
	sqlstr = "create table mandb_md5(md5_hash)";

	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
		
	sqlite3_finalize(stmt);

/*------------------------ Build the mandb_tf table------------------------------ */	
	sqlstr = "create table mandb_tf(docid, term, tf)";

	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_finalize(stmt);

/*------------------------Build the mandb_idf table------------------------------ */	
	sqlstr = "create table mandb_idf(term, idf)";

	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	sqlite3_shutdown();
	return 0;
}

/*
* concat--
*  Utility function. Concatenates together: dst, a space character and src. 
* dst + " " + src 
*/
static int
concat(char **dst, const char *src)
{
	int total_len, dst_len;
	if (src == NULL)
		return -1;

	/* we should allow the destination string to be NULL */
	if (*dst == NULL)
		dst_len = 0;
	else	
		dst_len = strlen(*dst);
	
	/* calculate total string length:
	*one extra character for a space and one for the nul byte 
	*/	
	total_len = dst_len + strlen(src) + 2;
		
	if ((*dst = (char *) realloc(*dst, total_len)) == NULL)
		return -1;
		
	if (*dst != NULL) {	
		memcpy(*dst + dst_len, " ", 1);
		dst_len++;
	}
	memcpy(*dst + dst_len, src, strlen(src) + 1);
	
	return 0;
}

/*
* check_md5--
*  Generates the md5 hash of the file and checks if it already doesn't exist in 
*  the database. This function is being used to avoid hardlinks.
*  Return values: 0 if the md5 hash does not exist in the datbase
*                 1 if the hash exists in the database
*                 -1 if an error occured while generating the hash or checking
*                 against the database
*/
static int 
check_md5(const char *file)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;

	assert(file != NULL);
	char *buf = MD5File(file, NULL);
	if (buf == NULL) {
		fprintf(stderr, "md5 failed: %s\n", file);
		return -1;
	}
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return -1;
	}

	sqlstr = "select * from mandb_md5 where md5_hash = :md5_hash";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
	rc = sqlite3_bind_text(stmt, idx, buf, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return -1;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		sqlite3_finalize(stmt);	
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return 1;
	}
	
	md5_hash = strdup(buf);
	sqlite3_finalize(stmt);	
	sqlite3_close(db);
	sqlite3_shutdown();
	free(buf);
	return 0;
}

/*
* build_term_weights--
*  Fetches a list of all unique terms from the mandb_aux table.
*  Computes the term-frequency and inverse document frequency for each term
*  and stores these weights in the mandb_tf and mandb_idf tables respectively
*/
static int
build_term_weights(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	char *term = NULL;
	sqlite3_stmt *stmt = NULL;
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_extended_result_codes(db, 1);
	
	sqlstr = "select distinct term from mandb_aux";
          
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		term = (char *) sqlite3_column_text(stmt, 0);
		if (is_stopword(term))
			continue;
		
		printf("Computing weight of %s\n", term);
		if (compute_term_weight(db, term) < 0) {
			continue;
		}
	}
	
	sqlite3_finalize(stmt);	
	sqlite3_close(db);
	sqlite3_shutdown();
	
	return 0;
}

/*
* compute_term_weight--
*  Computes the term frequency and inverse document frequency of the individual
*  terms in the corpus.
*  tf = (hitcount/globalhitcount)* weight 
*  hitcount = Number of times the term appears in a column in the current document
*  globalhitcount = Number of times it appears in the same column in all the documents
*  weight = a static weight assigned to a column
*  The final tf is the sum over all the columns in the table
*
*  idf = log(ndoc/ndocshitcount)
*  ndoc = Total number of documents in the corpus
*  ndocshitcount = total number of documents in which the term appears
*/
static int
compute_term_weight(sqlite3 *db, const char *term)
{
	int rc = 0;
	int idx = -1;
	int *matchinfo;
	int nphrase;	//Number of phrases
	int ncol;		//Number of columns in the table
	int iphrase;
	int ndocshitcount;	//Number of documents in which a term appears
	double weights[3] = {2.0, 1.5, 0.75};	//weights for the 3 columns of the table
	double tf_weight = 0.0;	//For the final tf
	double idf_weight = 0.0;	//For the final idf
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	term_frequency *tf = NULL;
	inverse_document_frequency *idf = NULL;
		
	sqlstr = "select docid, matchinfo(mandb) from mandb where mandb match :term";
          
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":term");
	rc = sqlite3_bind_text(stmt, idx, term, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		tf_weight = 0.0;
		idf_weight = 0.0;
		tf = malloc(sizeof(term_frequency));
		if (tf == NULL)
			errx(EXIT_FAILURE, "malloc failed\n");
		tf->term = term;
		tf->docid = (int) sqlite3_column_int(stmt, 0);
		if (idf == NULL) {
			idf = malloc(sizeof(inverse_document_frequency));
			idf->term = (char *) term;
			idf->status = 0;
			idf->idf = 0.0;
		}
		matchinfo = (unsigned int *) sqlite3_column_blob(stmt, 1);
		nphrase = matchinfo[0];
		ncol = matchinfo[1];
		int ndoc = get_ndoc();
		if (ndoc == 0)
			return -1;
		
		for(iphrase = 0; iphrase < nphrase; iphrase++) {
			int icol;
			int *phraseinfo = &matchinfo[2 + iphrase * ncol * 3];
		
			for(icol = 1; icol < ncol; icol++) {
		  		int nhitcount = phraseinfo[3*icol];
				int nglobalhitcount = phraseinfo[3*icol+1];
				double weight = weights[icol - 1];
				int ndocshitcount = phraseinfo[3 * icol + 2]; 
				if (nglobalhitcount > 0)
					tf_weight += ((double)nhitcount / nglobalhitcount) * weight;
			  
				if (idf->status == 0 && ndocshitcount > 0)
				  	idf_weight += log((double)ndoc / ndocshitcount) / log(ndoc);
			}
		
			tf->tf = tf_weight;
			idf->idf = idf_weight;
			
			if (idf != NULL && idf->status == 0) {
				store_idf(db, idf);
				idf->status = 1;
			}
					
			store_tf(db, tf);
			free(tf);
		}
		
	}
	free(idf);
	sqlite3_finalize(stmt);
	return 0;

}

static int
store_tf(sqlite3 *db, term_frequency *tf)
{
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	
/*-----------------------Populate the mandb_tf table-------------------------*/
	sqlstr = "insert into mandb_tf values (:docid, :term, :tf)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		return -1;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":docid");
	rc = sqlite3_bind_int(stmt, idx, tf->docid);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":term");
	rc = sqlite3_bind_text(stmt, idx, tf->term, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":tf");
	rc = sqlite3_bind_double(stmt, idx, tf->tf);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	sqlite3_finalize(stmt);
	return 0;
}

static int
store_idf(sqlite3 *db, inverse_document_frequency *idf)
{
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;

/*-----------------------Populate the mandb_idf table-------------------------*/
	sqlstr = "insert into mandb_idf values(:term, :idf)";
	
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":term");
	rc = sqlite3_bind_text(stmt, idx, idf->term, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":idf");
	rc = sqlite3_bind_double(stmt, idx, idf->idf);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	sqlite3_finalize(stmt);
	return 0;
}

/*
* get_ndoc--
*  Utility function. Returns the total number of documents in the corpus
*/
static int
get_ndoc(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int ndoc = 0;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;

		
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0;
	}

	sqlstr = "select count(name) from mandb";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		ndoc = (int) sqlite3_column_int(stmt, 0);
	}

	sqlite3_finalize(stmt);	
	sqlite3_close(db);
	sqlite3_shutdown();
	return ndoc;
}

/*
* term_exists--
*  Checks if the weight of this term has already been computed in the database.
*  Returns: Positive value, if it has been computed or an error occured in checking.
*			Else returns 0, which means, we need to compute it's weight.
*/
static int
term_exists(sqlite3 *db, const char *term)
{
	int rc = 0;
	int idx = -1;
	int ret_val = 0;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	
	sqlstr = "select term from mandb_tf where term = :term";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":term");
	rc = sqlite3_bind_text(stmt, idx, term, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) 
		ret_val = 1;
	
	sqlite3_finalize(stmt);
	return ret_val;
}

/*
* is_stopword--
*  Returns 1 if term is a stopword, else returns 0
*/
static int
is_stopword(const char *term)
{
	int i = 0;
	char *temp;
	assert(term != NULL);
	char *stopwords[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",
	"m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "0", "1",
	"2", "3", "4", "5", "6", "7", "8", "9", "again", "willing", "always", "any", 
	"around", "ask", "back", "been", "case", "did", "does", "down", "each", "early","either",
	"end", "enough", "even", "every", "fact", "far", "few", "four", "further", "general", "good",
	"got", "great", "having", "high", "him", "his", "however", "if", "important", "in", "interest",
	"into", "it", "just", "keep", "keeps", "kind", "knew", "know", "large", "larger", "last", "later",
	"latter", "latest", "least", "let", "like", "likely", "long", "longer", "made", "many", "may", "me",
	"might", "most", "mostly", "much", "must", "my", "necessari", "need", "never", "needs", "next", "no",
	"non", "noone", "not", "nothing", "now", "number", "often", "old", "older", "once", "onli", "order", 
	"our", "out", "over", "part", "per", "perhaps", "possible", "present", "problem", "quite", "rather",
	"really", "right", "room", "said", "same", "saw", "say", "says", "second", "see", "seem", "seemed", "seems",
	"sees", "several", "shall", "should", "side", "sides", "small", "smaller", "so", "some", "something", "state",
	"states", "still", "such", "sure", "take", "taken", "then", "them", "their", "there", "therefore", "thing",
	"think", "thinks", "though", "three", "thus", "together", "too", "took", "toward", "turn", "two", "until",
	"upon", "us", "use", "used", "uses", "very", "want", "wanted", "wants", "was", "way", "ways", "we", "well",
	"went", "were", "whether", "with", "within", "without", "work", "would", "year", "yet", "you",
	 "about", "also", "all", "an", "another", "and", "are", "as",
	"at", "be", "befor", "between", "below", "by", "bye", "but", "can", "consist",
	"could", "follow", "from", "full", "give", "given", "have", "has", "had", "here", 
	"how", "is", "names", "of", "off", "on", "or", "the", "this", "up",
	"that", "to", "new", "what", "when", 
	"why", "will", "because", "these", "those",  NULL};
	for (temp = stopwords[i]; temp != NULL; temp = stopwords[i++]) {
		if (!strcmp(temp, term))
		return 1;
	}
	
	return 0;
}	
