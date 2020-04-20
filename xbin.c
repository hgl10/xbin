/*
** 2020-04-03 by hgl10
** xbin interface for sqlite virtual table
**
** .load xbin
** create virtual table xbin using xbin(./test.bin);
** select count(*) from xbin;
** .timer on
** select rowid, * from xbin where rowid > 100000 order by rowid limit 10;
** delete from xbin where rowid = 1;
*/

#if !defined(SQLITEINT_H)
#include "sqlite3ext.h"
#endif
SQLITE_EXTENSION_INIT1
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

typedef struct xbinData {
  float id;
  float iq;
  float speed;
  float torque;
  float Ld;
  float Lq;
  float Lambda;
  float Rs;
  float Temp;
} xbinData;

/* XbinTable is a subclass of sqlite3_vtab which is
** underlying representation of the virtual table
*/
typedef struct XbinTable {
  sqlite3_vtab base;  /* Base class - must be first */
  char *filename;     /* Name of the xbin file */
  FILE *fptr;         /* used to scan file */
} XbinTable;

/* XbinCursor is a subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result
*/
typedef struct XbinCursor {
  sqlite3_vtab_cursor base;   /* Base class - must be first */
  FILE *fptr;                 /* used to scan file */
  sqlite3_int64 row;          /* The rowid */
  xbinData data;
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
) {
  XbinTable *pTab;
  int rc;
  const char *filename = argv[3];
  if ( argc != 4 ) return SQLITE_ERROR;

  pTab = sqlite3_malloc( sizeof(*pTab) );
  *ppVtab = (sqlite3_vtab*)pTab;
  if ( pTab == 0 ) return SQLITE_NOMEM;
  memset(pTab, 0, sizeof(*pTab));

  pTab->filename = sqlite3_mprintf( "%s", filename );

  rc = sqlite3_declare_vtab(db,
                            "CREATE TABLE x(row INTEGER PRIMARY KEY, id REAL, iq REAL, speed REAL, torque REAL, ld REAL, lq REAL, lambda REAL, Rs REAL, temp REAL)"
                           );

  if ( rc != SQLITE_OK ) {
    sqlite3_free( pTab->filename );
    sqlite3_free( pTab );
    return SQLITE_ERROR;
  }

  pTab->fptr = fopen( pTab->filename, "r+b" );
  if ( pTab->fptr == NULL ) {
    sqlite3_free(pTab->base.zErrMsg);
    pTab->base.zErrMsg = sqlite3_mprintf("==> Database File Not Found!");
    sqlite3_free( pTab->filename );
    sqlite3_free( pTab );
    return SQLITE_ERROR;
  }

  return rc;
}

/*
** This method is the destructor for XbinTable objects.
*/
static int xbinDisconnect(sqlite3_vtab *pVtab) {
  XbinTable *pTab = (XbinTable*)pVtab;
  if (pTab->fptr != NULL) {
    fclose(pTab->fptr);
  }
  sqlite3_free( pTab->filename );
  sqlite3_free(pTab);
  return SQLITE_OK;
}

/*
** Constructor for a new XbinCursor object.
*/
static int xbinOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **cur) {
  XbinTable   *pTab = (XbinTable*) p;
  XbinCursor  *pCur;
  FILE        *fptr;

  pCur = sqlite3_malloc( sizeof(*pCur) );
  if ( pCur == 0 ) {
    return SQLITE_NOMEM;
  }
  memset(pCur, 0, sizeof(*pCur));

  pCur->fptr = pTab->fptr;
  *cur = &pCur->base;
  return SQLITE_OK;
}

/*
** Destructor for a XbinCursor.
*/
static int xbinClose(sqlite3_vtab_cursor *cur) {
  XbinCursor *pCur = (XbinCursor*)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int xbin_get_line( XbinCursor *pCur ) {
  pCur->row ++;
  fread(&pCur->data, sizeof(xbinData), 1, pCur->fptr);

  return SQLITE_OK;
}

/*
** Advance a XbinCursor to its next row of output.
*/
static int xbinNext(sqlite3_vtab_cursor *cur) {
  return xbin_get_line((XbinCursor*)cur);
}

/*
** Return values of columns for the row at which the XbinCursor
** is currently pointing.
*/
static int xbinColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
) {
  XbinCursor *pCur = (XbinCursor*)cur;
  if (i == 0) {
    sqlite3_result_int64(ctx, pCur->row);
    return SQLITE_OK;
  }
  float *start = (float *) & (pCur->data);
  sqlite3_result_double(ctx, (double)start[i - 1]);
  return SQLITE_OK;
}

/*
** Return the rowid for the current row, just same as the row number.
*/
static int xbinRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  *pRowid = ((XbinCursor*)cur)->row;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int xbinEof(sqlite3_vtab_cursor *cur) {
  XbinCursor* pCur = (XbinCursor*) cur;
  return feof(pCur->fptr);
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
) {
  XbinCursor *pCur = (XbinCursor *)pVtabCursor;
  if (idxNum == 0) {
    fseek( pCur->fptr, 0, SEEK_SET );
    pCur->row = 0;
    return xbin_get_line(pCur);
  } else if (idxNum == 1) {
    pCur->row = sqlite3_value_int64(argv[0]) - 1;
    fseek( pCur->fptr, (pCur->row) * sizeof(xbinData), SEEK_SET );
    return xbin_get_line(pCur);
  }
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
) {
  int i;
  int idx = -1;

  for (i = 0; i < pIdxInfo->nOrderBy; i++) {
    /*for rowid*/
    if ( pIdxInfo->aOrderBy[i].iColumn == 0 ) {
      pIdxInfo->orderByConsumed = 1;
      break;
    }
  }

  for (i = 0; i < pIdxInfo->nConstraint; i++) {
    if ( pIdxInfo->aConstraint[i].iColumn != 0 ) continue;
    if ( pIdxInfo->aConstraint[i].usable == 0 ) {
      return SQLITE_CONSTRAINT;
    }
    if ( (pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ) || (pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_GE) ) {
      idx = i;
    }
  }

  if ( idx >= 0 ) {
    pIdxInfo->aConstraintUsage[idx].argvIndex = 1;
    pIdxInfo->aConstraintUsage[idx].omit = 1;
    pIdxInfo->estimatedRows = 1;
    pIdxInfo->idxNum = 1;
    pIdxInfo->estimatedCost = 1.0;
    pIdxInfo->idxFlags = SQLITE_INDEX_SCAN_UNIQUE;
    return SQLITE_OK;
  }

  pIdxInfo->idxNum = 0;
  pIdxInfo->estimatedCost = 100000.0;
  pIdxInfo->estimatedRows = 100000;
  return SQLITE_OK;
}

static int xbinUpdate(
  sqlite3_vtab *vtab,
  int argc, sqlite3_value **argv,
  sqlite_int64 *rowid
) {
  XbinTable* pTab = (XbinTable*) vtab;
  if (argc == 1) {
    // argc = 1
    // argv[0] ≠ NULL
    // DELETE: The single row with rowid or PRIMARY KEY equal to argv[0] is deleted. No insert occurs.
    sqlite3_free(pTab->base.zErrMsg);
    pTab->base.zErrMsg = sqlite3_mprintf("Delete Error: delete is disabled by default.");
    return SQLITE_ERROR;
  } else if ((argc > 1) && (sqlite3_value_type(argv[0]) == SQLITE_NULL)) {
    // argc > 1
    // argv[0] = NULL
    // INSERT: A new row is inserted with column values taken from argv[2] and following.
    // In a rowid virtual table, if argv[1] is an SQL NULL, then a new unique rowid is generated automatically.
    fseek(pTab->fptr, 0, SEEK_END);

    xbinData data;
    data.id = sqlite3_value_double(argv[3]);
    data.iq = sqlite3_value_double(argv[4]);
    data.speed = sqlite3_value_double(argv[5]);
    data.torque = sqlite3_value_double(argv[6]);
    data.Ld = sqlite3_value_double(argv[7]);
    data.Lq = sqlite3_value_double(argv[8]);
    data.Lambda = sqlite3_value_double(argv[9]);
    data.Rs = sqlite3_value_double(argv[10]);
    data.Temp = sqlite3_value_double(argv[11]);

    fwrite(&data, sizeof(xbinData), 1, pTab->fptr);
  }
  return SQLITE_OK;
}

/*
** This following structure defines all the methods for the
** virtual table.
*/
static sqlite3_module xbinModule = {
  /* iVersion    */ 0,
  /* xCreate     */ xbinConnect,
  /* xConnect    */ xbinConnect,
  /* xBestIndex  */ xbinBestIndex,
  /* xDisconnect */ xbinDisconnect,
  /* xDestroy    */ xbinDisconnect,
  /* xOpen       */ xbinOpen,
  /* xClose      */ xbinClose,
  /* xFilter     */ xbinFilter,
  /* xNext       */ xbinNext,
  /* xEof        */ xbinEof,
  /* xColumn     */ xbinColumn,
  /* xRowid      */ xbinRowid,
  /* xUpdate     */ xbinUpdate,
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
) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  rc = sqlite3_create_module(db, "xbin", &xbinModule, NULL);
  return rc;
}
