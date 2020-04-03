/*
** 2020-04-03 by hgl10
** xbin interface for sqlite virtual table
** 
** This template implements an eponymous-only virtual table with a rowid and
** two columns named "a" and "b".  The table as 10 rows with fixed integer
** values. Usage example:
**
**     SELECT rowid, a, b FROM xbin;
*/
#if !defined(SQLITEINT_H)
#include "sqlite3ext.h"
#endif
SQLITE_EXTENSION_INIT1
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>

/* Max size of the error message in a XbinReader */
#define XBIN_MXERR 200

/* Size of the XbinReader input buffer */
#define XBIN_INBUFSZ 64

/* A context object used when read a Xbin file. */
typedef struct XbinReader {
  FILE *in;              /* Read the CSV text from this input stream */
  char *z;               /* Accumulated text for a field */
  int n;                 /* Number of bytes in z */
  int nAlloc;            /* Space allocated for z[] */
  int nLine;             /* Current line number */
  int bNotFirst;         /* True if prior text has been seen */
  int cTerm;             /* Character that terminated the most recent field */
  size_t iIn;            /* Next unread character in the input buffer */
  size_t nIn;            /* Number of characters in the input buffer */
  char *zIn;             /* The input buffer */
  char zErr[XBIN_MXERR];  /* Error message */
} XbinReader;

/* Initialize a XbinReader object */
static void xbin_reader_init(XbinReader *p){
  p->in = 0;
  p->z = 0;
  p->n = 0;
  p->nAlloc = 0;
  p->nLine = 0;
  p->bNotFirst = 0;
  p->nIn = 0;
  p->zIn = 0;
  p->zErr[0] = 0;
}

/* Close and reset a XbinReader object */
static void xbin_reader_reset(XbinReader *p){
  if( p->in ){
    fclose(p->in);
    sqlite3_free(p->zIn);
  }
  sqlite3_free(p->z);
  xbin_reader_init(p);
}

/* Report an error on a XbinReader */
static void xbin_errmsg(XbinReader *p, const char *zFormat, ...){
  va_list ap;
  va_start(ap, zFormat);
  sqlite3_vsnprintf(XBIN_MXERR, p->zErr, zFormat, ap);
  va_end(ap);
}

/* Open the file associated with a XbinReader
** Return the number of errors.
*/
static int xbin_reader_open(
  XbinReader *p,               /* The reader to open */
  const char *zFilename       /* Read from this filename */
){
  p->zIn = sqlite3_malloc( XBIN_INBUFSZ );
  if( p->zIn==0 ){
    xbin_errmsg(p, "out of memory");
    return 1;
  }
  p->in = fopen(zFilename, "rb");
  if( p->in==0 ){
    sqlite3_free(p->zIn);
    xbin_reader_reset(p);
    xbin_errmsg(p, "cannot open '%s' for reading", zFilename);
    return 1;
  }
  return 0;
}

/* Forward references to the various virtual table methods implemented
** in this file. */
static int xbinTabCreate(sqlite3*, void*, int, const char*const*, 
                           sqlite3_vtab**,char**);
static int xbinTabConnect(sqlite3*, void*, int, const char*const*, 
                           sqlite3_vtab**,char**);
static int xbinTabBestIndex(sqlite3_vtab*,sqlite3_index_info*);
static int xbinTabDisconnect(sqlite3_vtab*);
static int xbinTabOpen(sqlite3_vtab*, sqlite3_vtab_cursor**);
static int xbinTabClose(sqlite3_vtab_cursor*);
static int xbinTabFilter(sqlite3_vtab_cursor*, int idxNum, const char *idxStr,
                          int argc, sqlite3_value **argv);
static int xbinTabNext(sqlite3_vtab_cursor*);
static int xbinTabEof(sqlite3_vtab_cursor*);
static int xbinTabColumn(sqlite3_vtab_cursor*,sqlite3_context*,int);
static int xbinTabRowid(sqlite3_vtab_cursor*,sqlite3_int64*);

/* XbinTable is a subclass of sqlite3_vtab which is
** underlying representation of the virtual table
*/
typedef struct XbinTable {
  sqlite3_vtab base;  /* Base class - must be first */
  char *zFilename;    /* Name of the xbin file */
  long iStart;        /* Offset to start of data in zFilename */
} XbinTable;

/* XbinCursor is a subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result
*/
typedef struct XbinCursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  sqlite3_int64 iRowid;      /* The rowid */
} XbinCursor;

/*
** The xbinConnect() method is invoked to create a new
** template virtual table.
**
** Think of this routine as the constructor for XbinTable objects.
**
** All this routine needs to do is:
**
**    (1) Allocate the XbinTable object and initialize all fields.
**
**    (2) Tell SQLite (via the sqlite3_declare_vtab() interface) what the
**        result set of queries against the virtual table will look like.
*/
static int xbinConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  XbinTable *pNew;
  int rc;

  rc = sqlite3_declare_vtab(db,
           "CREATE TABLE x(a INTEGER,b INTEGER)"
       );
  /* For convenience, define symbolic names for the index to each column. */
#define XBIN_A  0
#define XBIN_B  1
  if( rc==SQLITE_OK ){
    pNew = sqlite3_malloc( sizeof(*pNew) );
    *ppVtab = (sqlite3_vtab*)pNew;
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
  }
  return rc;
}

/*
** This method is the destructor for XbinTable objects.
*/
static int xbinDisconnect(sqlite3_vtab *pVtab){
  XbinTable *p = (XbinTable*)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

/*
** Constructor for a new XbinCursor object.
*/
static int xbinOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  XbinCursor *pCur;
  pCur = sqlite3_malloc( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

/*
** Destructor for a XbinCursor.
*/
static int xbinClose(sqlite3_vtab_cursor *cur){
  XbinCursor *pCur = (XbinCursor*)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}


/*
** Advance a XbinCursor to its next row of output.
*/
static int xbinNext(sqlite3_vtab_cursor *cur){
  XbinCursor *pCur = (XbinCursor*)cur;
  pCur->iRowid++;
  return SQLITE_OK;
}

/*
** Return values of columns for the row at which the XbinCursor
** is currently pointing.
*/
static int xbinColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  XbinCursor *pCur = (XbinCursor*)cur;
  switch( i ){
    case XBIN_A:
      sqlite3_result_int(ctx, 1000 + pCur->iRowid);
      break;
    default:
      assert( i==XBIN_B );
      sqlite3_result_int(ctx, 2000 + pCur->iRowid);
      break;
  }
  return SQLITE_OK;
}

/*
** Return the rowid for the current row.  In this implementation, the
** rowid is the same as the output value.
*/
static int xbinRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  XbinCursor *pCur = (XbinCursor*)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int xbinEof(sqlite3_vtab_cursor *cur){
  XbinCursor *pCur = (XbinCursor*)cur;
  return pCur->iRowid>=10;
}

/*
** This method is called to "rewind" the XbinCursor object back
** to the first row of output.  This method is always called at least
** once prior to any call to xbinColumn() or xbinRowid() or 
** xbinEof().
*/
static int xbinFilter(
  sqlite3_vtab_cursor *pVtabCursor, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  XbinCursor *pCur = (XbinCursor *)pVtabCursor;
  pCur->iRowid = 1;
  return SQLITE_OK;
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
*/
static int xbinBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

/*
** This following structure defines all the methods for the 
** virtual table.
*/
static sqlite3_module xbinModule = {
  /* iVersion    */ 0,
  /* xCreate     */ 0,
  /* xConnect    */ xbinConnect,
  /* xBestIndex  */ xbinBestIndex,
  /* xDisconnect */ xbinDisconnect,
  /* xDestroy    */ 0,
  /* xOpen       */ xbinOpen,
  /* xClose      */ xbinClose,
  /* xFilter     */ xbinFilter,
  /* xNext       */ xbinNext,
  /* xEof        */ xbinEof,
  /* xColumn     */ xbinColumn,
  /* xRowid      */ xbinRowid,
  /* xUpdate     */ 0,
  /* xBegin      */ 0,
  /* xSync       */ 0,
  /* xCommit     */ 0,
  /* xRollback   */ 0,
  /* xFindMethod */ 0,
  /* xRename     */ 0,
  /* xSavepoint  */ 0,
  /* xRelease    */ 0,
  /* xRollbackTo */ 0,
  /* xShadowName */ 0
};


#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_xbin_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  rc = sqlite3_create_module(db, "xbin", &xbinModule, 0);
  return rc;
}
