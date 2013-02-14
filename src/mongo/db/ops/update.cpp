//@file update.cpp

/**
 *    Copyright (C) 2008 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"

#include "mongo/db/oplog.h"
#include "mongo/db/queryutil.h"
#include "mongo/client/dbclientinterface.h"

#include "update.h"
#include "update_internal.h"

//#define DEBUGUPDATE(x) cout << x << endl;
#define DEBUGUPDATE(x)

namespace mongo {

    // TODO: Re-enable these when we need them
    void checkNoMods( BSONObj o ) {
        BSONObjIterator i( o );
        while( i.moreWithEOO() ) {
            BSONElement e = i.next();
            if ( e.eoo() )
                break;
            uassert( 10154 ,  "Modifiers and non-modifiers cannot be mixed", e.fieldName()[ 0 ] != '$' );
        }
    }

    static void checkTooLarge(const BSONObj& newObj) {
        uassert( 12522 , "$ operator made object too large" , newObj.objsize() <= BSONObjMaxUserSize );
    }

    /* note: this is only (as-is) called for

             - not multi
             - not mods is indexed
             - not upsert
    */
    static UpdateResult _updateById(bool isOperatorUpdate,
                                    int idIdxNo,
                                    ModSet* mods,
                                    int profile,
                                    NamespaceDetails* d,
                                    NamespaceDetailsTransient *nsdt,
                                    bool su,
                                    const char* ns,
                                    const BSONObj& updateobj,
                                    BSONObj patternOrig,
                                    bool logop,
                                    OpDebug& debug,
                                    bool fromMigrate = false) {

        ::abort();
#if 0
        DiskLoc loc;
        {
            IndexDetails& i = d->idx(idIdxNo);
            BSONObj key = i.getKeyFromQuery( patternOrig );
            loc = i.idxInterface().findSingle(i, i.head, key);
            if( loc.isNull() ) {
                // no upsert support in _updateById yet, so we are done.
                return UpdateResult( 0 , 0 , 0 , BSONObj() );
            }
        }
        Record* r = loc.rec();

        if ( cc().allowedToThrowPageFaultException() && ! r->likelyInPhysicalMemory() ) {
            throw PageFaultException( r );
        }
#endif

        /* look for $inc etc.  note as listed here, all fields to inc must be this type, you can't set some
           regular ones at the moment. */
        if ( isOperatorUpdate ) {
            ::abort();
#if 0
            const BSONObj& onDisk = loc.obj();
            auto_ptr<ModSetState> mss = mods->prepare( onDisk );

            if( mss->canApplyInPlace() ) {
                mss->applyModsInPlace(true);
                DEBUGUPDATE( "\t\t\t updateById doing in place update" );
            }
            else {
                BSONObj newObj = mss->createNewFromMods();
                checkTooLarge(newObj);
                verify(nsdt);
                ::abort(); //theDataFileMgr.updateRecord(ns, d, nsdt, r, loc , newObj.objdata(), newObj.objsize(), debug);
            }

            if ( logop ) {
                DEV verify( mods->size() );

                BSONObj pattern = patternOrig;
                if ( mss->haveArrayDepMod() ) {
                    BSONObjBuilder patternBuilder;
                    patternBuilder.appendElements( pattern );
                    mss->appendSizeSpecForArrayDepMods( patternBuilder );
                    pattern = patternBuilder.obj();
                }

                if( mss->needOpLogRewrite() ) {
                    DEBUGUPDATE( "\t rewrite update: " << mss->getOpLogRewrite() );
                    logOp("u", ns, mss->getOpLogRewrite() ,
                          &pattern, 0, fromMigrate );
                }
                else {
                    logOp("u", ns, updateobj, &pattern, 0, fromMigrate );
                }
            }
            return UpdateResult( 1 , 1 , 1 , BSONObj() );
#endif
        } // end $operator update

        // regular update
        BSONElementManipulator::lookForTimestamps( updateobj );
        checkNoMods( updateobj );
        verify(nsdt);
        ::abort(); //theDataFileMgr.updateRecord(ns, d, nsdt, r, loc , updateobj.objdata(), updateobj.objsize(), debug );
        if ( logop ) {
            logOp("u", ns, updateobj, &patternOrig, 0, fromMigrate );
        }
        return UpdateResult( 1 , 0 , 1 , BSONObj() );
    }

    UpdateResult _updateObjects( bool su,
                                 const char* ns,
                                 const BSONObj& updateobj,
                                 const BSONObj& patternOrig,
                                 bool upsert,
                                 bool multi,
                                 bool logop ,
                                 OpDebug& debug,
                                 /*RemoveSaver* rs,*/
                                 bool fromMigrate,
                                 const QueryPlanSelectionPolicy& planPolicy ) {

        DEBUGUPDATE( "update: " << ns
                     << " update: " << updateobj
                     << " query: " << patternOrig
                     << " upsert: " << upsert << " multi: " << multi );

        Client& client = cc();
        int profile = client.database()->profile;

        debug.updateobj = updateobj;

        // The idea with these here it to make them loop invariant for
        // multi updates, and thus be a bit faster for that case.  The
        // pointers may be left invalid on a failed or terminal yield
        // recovery.
        NamespaceDetails* d = nsdetails(ns); // can be null if an upsert...
        NamespaceDetailsTransient* nsdt = &NamespaceDetailsTransient::get(ns);

        auto_ptr<ModSet> mods;
        bool isOperatorUpdate = updateobj.firstElementFieldName()[0] == '$';
        int modsIsIndexed = false; // really the # of indexes
        if ( isOperatorUpdate ) {
            if( d && d->indexBuildInProgress ) {
                set<string> bgKeys;
                d->inProgIdx().keyPattern().getFieldNames(bgKeys);
                mods.reset( new ModSet(updateobj, nsdt->indexKeys(), &bgKeys) );
            }
            else {
                mods.reset( new ModSet(updateobj, nsdt->indexKeys()) );
            }
            modsIsIndexed = mods->isIndexed();
        }

        if( planPolicy.permitOptimalIdPlan() && !multi && isSimpleIdQuery(patternOrig) && d &&
           !modsIsIndexed ) {
            int idxNo = d->findIdIndex();
            if( idxNo >= 0 ) {
                debug.idhack = true;

                UpdateResult result = _updateById( isOperatorUpdate,
                                                   idxNo,
                                                   mods.get(),
                                                   profile,
                                                   d,
                                                   nsdt,
                                                   su,
                                                   ns,
                                                   updateobj,
                                                   patternOrig,
                                                   logop,
                                                   debug,
                                                   fromMigrate);
                if ( result.existing || ! upsert ) {
                    return result;
                }
                else if ( upsert && ! isOperatorUpdate && ! logop) {
                    // this handles repl inserts
                    checkNoMods( updateobj );
                    debug.upsert = true;
                    BSONObj no = updateobj;
                    ::abort(); //theDataFileMgr.insertWithObjMod(ns, no, su);
                    return UpdateResult( 0 , 0 , 1 , no );
                }
            }
        }

        int numModded = 0;
        debug.nscanned = 0;
        shared_ptr<Cursor> c =
            NamespaceDetailsTransient::getCursor( ns, patternOrig, BSONObj(), planPolicy );
        d = nsdetails(ns);
        nsdt = &NamespaceDetailsTransient::get(ns);

        if( c->ok() ) {
            set<BSONObj> seenObjects;
            MatchDetails details;
            auto_ptr<ClientCursor> cc;
            do {

                bool atomic = c->matcher() && c->matcher()->docMatcher().atomic();

                if ( ! atomic && debug.nscanned > 0 ) {
                    // TODO: Is this still true, since yielding has been removed?
                    // we need to use a ClientCursor to yield
                    if ( cc.get() == 0 ) {
                        shared_ptr< Cursor > cPtr = c;
                        cc.reset( new ClientCursor( QueryOption_NoCursorTimeout , cPtr , ns ) );
                    }

                    if ( !c->ok() ) {
                        break;
                    }
                }

                debug.nscanned++;

                if ( mods.get() && mods->hasDynamicArray() ) {
                    // The Cursor must have a Matcher to record an elemMatchKey.  But currently
                    // a modifier on a dynamic array field may be applied even if there is no
                    // elemMatchKey, so a matcher cannot be required.
                    //verify( c->matcher() );
                    details.requestElemMatchKey();
                }

                if ( !c->currentMatches( &details ) ) {
                    c->advance();
                    continue;
                }

                BSONObj currPK = c->currPK();
                if ( c->getsetdup( currPK ) ) {
                    c->advance();
                    continue;
                }

                BSONObj currentObj = c->current();
                BSONObj pattern = patternOrig;

                if ( logop ) {
                    BSONObjBuilder idPattern;
                    BSONElement id;
                    // NOTE: If the matching object lacks an id, we'll log
                    // with the original pattern.  This isn't replay-safe.
                    // It might make sense to suppress the log instead
                    // if there's no id.
                    if ( currentObj.getObjectID( id ) ) {
                        idPattern.append( id );
                        pattern = idPattern.obj();
                    }
                    else {
                        uassert( 10157 ,  "multi-update requires all modified objects to have an _id" , ! multi );
                    }
                }

                /* look for $inc etc.  note as listed here, all fields to inc must be this type, you can't set some
                    regular ones at the moment. */
                if ( isOperatorUpdate ) {

                    if ( multi ) {
                        // go to next record in case this one moves
                        c->advance();

                        if ( seenObjects.count( currPK ) ) {
                            continue;
                        }

                        // SERVER-5198 Advance past the document to be modified, provided
                        // deduplication is enabled, but see SERVER-5725.
                        // TODO: Look up SERVER-5725 and see if it matters to TokuDB
                        while( c->ok() && currPK == c->currPK() ) {
                            c->advance();
                        }
                    }

                    ModSet* useMods = mods.get();
                    bool forceRewrite = false;

                    auto_ptr<ModSet> mymodset;
                    if ( details.hasElemMatchKey() && mods->hasDynamicArray() ) {
                        useMods = mods->fixDynamicArray( details.elemMatchKey() );
                        mymodset.reset( useMods );
                        forceRewrite = true;
                    }

                    auto_ptr<ModSetState> mss = useMods->prepare( currentObj );
                    
#if 0
                    // tokudb: modsIsIndexed must be true if there exists at least one clustering index
                    for (int idx_i = 0; idx_i < d->nIndexesBeingBuilt(); idx_i++) {
                        IndexDetails &idx = d->idx(idx_i);
                        if (idx.info.obj()["clustering"].trueValue()) {
                            modsIsIndexed = true;
                            break;
                        }
                    }
#endif

                    if ( modsIsIndexed <= 0 && mss->canApplyInPlace() ) {
                        mss->applyModsInPlace( true );

                        DEBUGUPDATE( "\t\t\t doing in place update" );
                        if ( profile && !multi )
                            debug.fastmod = true;

                        if ( modsIsIndexed ) {
                            seenObjects.insert( currPK );
                        }
                    }
                    else {
                        BSONObj newObj = mss->createNewFromMods();
                        checkTooLarge(newObj);
                        ::abort();
#if 0
                        theDataFileMgr.updateRecord(ns,
                                                    d,
                                                    nsdt,
                                                    r,
                                                    loc,
                                                    newObj.objdata(),
                                                    newObj.objsize(),
                                                    debug);
#endif

                    }

                    if ( logop ) {
                        DEV verify( mods->size() );

                        if ( mss->haveArrayDepMod() ) {
                            BSONObjBuilder patternBuilder;
                            patternBuilder.appendElements( pattern );
                            mss->appendSizeSpecForArrayDepMods( patternBuilder );
                            pattern = patternBuilder.obj();
                        }

                        if ( forceRewrite || mss->needOpLogRewrite() ) {
                            DEBUGUPDATE( "\t rewrite update: " << mss->getOpLogRewrite() );
                            logOp("u", ns, mss->getOpLogRewrite() ,
                                  &pattern, 0, fromMigrate );
                        }
                        else {
                            logOp("u", ns, updateobj, &pattern, 0, fromMigrate );
                        }
                    }
                    numModded++;
                    if ( ! multi )
                        return UpdateResult( 1 , 1 , numModded , BSONObj() );

                    // TODO: Vanilla mongo did commitIfNeeded her. What do we do?
                    continue;
                }

                uassert( 10158 ,  "multi update only works with $ operators" , ! multi );

                BSONElementManipulator::lookForTimestamps( updateobj );
                checkNoMods( updateobj );
                ::abort();
                //theDataFileMgr.updateRecord(ns, d, nsdt, r, loc , updateobj.objdata(), updateobj.objsize(), debug, su);
                if ( logop ) {
                    DEV wassert( !su ); // super used doesn't get logged, this would be bad.
                    logOp("u", ns, updateobj, &pattern, 0, fromMigrate );
                }
                return UpdateResult( 1 , 0 , 1 , BSONObj() );
            } while ( c->ok() );
        } // endif

        if ( numModded )
            return UpdateResult( 1 , 1 , numModded , BSONObj() );

        if ( upsert ) {
            if ( updateobj.firstElementFieldName()[0] == '$' ) {
                // upsert of an $operation. build a default object
                BSONObj newObj = mods->createNewFromQuery( patternOrig );
                checkNoMods( newObj );
                debug.fastmodinsert = true;
                ::abort(); //theDataFileMgr.insertWithObjMod(ns, newObj, su);
                if ( logop )
                    logOp( "i", ns, newObj, 0, 0, fromMigrate );

                return UpdateResult( 0 , 1 , 1 , newObj );
            }
            uassert( 10159 ,  "multi update only works with $ operators" , ! multi );
            checkNoMods( updateobj );
            debug.upsert = true;
            BSONObj no = updateobj;
            ::abort(); //theDataFileMgr.insertWithObjMod(ns, no, su);
            if ( logop )
                logOp( "i", ns, no, 0, 0, fromMigrate );
            return UpdateResult( 0 , 0 , 1 , no );
        }

        return UpdateResult( 0 , isOperatorUpdate , 0 , BSONObj() );
    }

    UpdateResult updateObjects( const char* ns,
                                const BSONObj& updateobj,
                                const BSONObj& patternOrig,
                                bool upsert,
                                bool multi,
                                bool logop ,
                                OpDebug& debug,
                                bool fromMigrate,
                                const QueryPlanSelectionPolicy& planPolicy ) {

        uassert( 10155 , "cannot update reserved $ collection", strchr(ns, '$') == 0 );
        if ( strstr(ns, ".system.") ) {
            /* dm: it's very important that system.indexes is never updated as IndexDetails has pointers into it */
            uassert( 10156,
                     str::stream() << "cannot update system collection: " << ns << " q: " << patternOrig << " u: " << updateobj,
                     legalClientSystemNS( ns , true ) );
        }

        UpdateResult ur = _updateObjects(false, ns, updateobj, patternOrig,
                                         upsert, multi, logop,
                                         debug, /*0,*/ fromMigrate, planPolicy );
        debug.nupdated = ur.num;
        return ur;
    }

    BSONObj applyUpdateOperators( const BSONObj& from, const BSONObj& operators ) {
        ModSet mods( operators );
        return mods.prepare( from )->createNewFromMods();
    }
    
}  // namespace mongo
