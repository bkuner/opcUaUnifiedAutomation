/*************************************************************************\
* Copyright (c) 2016 Helmholtz-Zentrum Berlin
*     fuer Materialien und Energie GmbH (HZB), Berlin, Germany.
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU Lesser General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
\*************************************************************************/

#include <stdlib.h>
#include <csignal>

#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>
#include <map>
#include <iterator>

// regex and stoi for lexical_cast are available as std functions in C11
//#include <regex> 
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

#include <epicsPrint.h>
#include <epicsExport.h>
#include <registryFunction.h>
#include <dbCommon.h>
#include <devSup.h>
#include <drvSup.h>
#include <devLib.h>
#include <iocsh.h>

#include "drvOpcUa.h"
#include "devUaSubscription.h"
#include "devUaSession.h"

using namespace UaClientSdk;

UaItem::UaItem(dbCommon *p)
{
    prec = p;
    stat = 1;       // not connected
    debug = (prec->tpro > 1) ? prec->tpro-1 : 0; // to avoid debug for habitual TPRO=1
    flagLock = epicsMutexMustCreate();
    isArray = 0;
    flagRdbkOff = 0;
}

// Maximize debug level driver-dbg (active >=1) and uaItem.debug set by record.TPRO
int UaItem::maxDebug(int dbg) {
    return (debug>dbg)?debug:dbg;
}

// return 0:ok, 1:data loss may occur, 2:not supported OPCUA-Type
// supposed EPICS-types: double, int32, uint32. As long as VAL,RVAL fields have no other types!
int UaItem::checkDataLoss()
{
    epicsType epicstype;
    if(inpDataType) {   // is OUT Record. TODO: what about records data conversion?
        epicstype = inpDataType;
    }
    else {
        epicstype = recDataType;
    }
    if(inpDataType) { // data loss warning for OUT-records
        switch(itemDataType){
        case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
            switch(epicstype){
            case epicsFloat64T: return 1;
            default:;
            }
            break;
        case OpcUaType_SByte:
        case OpcUaType_Byte:
        case OpcUaType_Int16:
        case OpcUaType_UInt16: return 1;
            break;

        case OpcUaType_Int32:
            switch(epicstype){//REC_Datatype(EPICS_Datatype)
            case epicsFloat64T: return 1;
            default:;
            }
            break;
        case OpcUaType_UInt32:
            switch(epicstype){//REC_Datatype(EPICS_Datatype)
            case epicsFloat64T: return 1;  break;
            default:;
            }
            break;
        case OpcUaType_Float:
            switch(epicstype){//REC_Datatype(EPICS_Datatype)
            case epicsFloat64T: return 1;  break;
            default:;
            }
            break;
        case OpcUaType_Double:
            break;
        case OpcUaType_String:
            if(epicstype != epicsOldStringT)
                return 1;
            break;
        default:
            return 2;
        }
    }
    else { // data loss warning for IN-records
        switch(epicstype){//REC_Datatype(EPICS_Datatype)
        case epicsInt32T:
            switch(itemDataType){
            case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
            case OpcUaType_SByte:
            case OpcUaType_Byte:
            case OpcUaType_Int16:
            case OpcUaType_UInt16:
            case OpcUaType_Int32:
            case OpcUaType_UInt32: return 0;
            case OpcUaType_Float:
            case OpcUaType_Double:
            case OpcUaType_String: return 1;
            default:
                return 2;
            }
        case epicsUInt32T:
                switch(itemDataType){
                case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
                case OpcUaType_SByte:
                case OpcUaType_Byte:
                case OpcUaType_Int16:
                case OpcUaType_UInt16:
                case OpcUaType_UInt32: return 0;
                case OpcUaType_Int32:
                case OpcUaType_Float:
                case OpcUaType_Double:
                case OpcUaType_String: return 1;
                default:
                    return 2;
                }
        case epicsFloat64T: return 0;
                switch(itemDataType){
                case OpcUaType_Boolean: // allow integer types to avoid warning for all booleans!
                case OpcUaType_SByte:
                case OpcUaType_Byte:
                case OpcUaType_Int16:
                case OpcUaType_UInt16:
                case OpcUaType_UInt32:
                case OpcUaType_Int32:
                case OpcUaType_Float:
                case OpcUaType_Double: return 0;
                case OpcUaType_String: return 1;
                default:
                    return 2;
                }
        case epicsOldStringT:
            if(itemDataType != OpcUaType_String)
                return 1;
            break;
        default: return 2;
        }

    }
    return 0;
}

// Parse the input link like: TAG;ns=<NS_IDX>;<IDTYPE>=<IDENTIFIER>
bool UaItem::parseLink(char *lnk)
{
    ItemPath = lnk;
    boost::regex rex;
    boost::cmatch matches;
    rex = std::string("([\\d\\w]+);\\s*ns=(\\d+);(\\w)=(.*)");
    char type;
    std::string sId;
    OpcUa_UInt16 ns=0;
    if (! boost::regex_match( lnk, matches, rex) || (matches.size() != 5)) {
        errlogPrintf("%s getNodes() SKIP for bad link. Can't parse '%s'\n",prec->name,ItemPath);
        return false;
    }
    tag  = matches[1];

    try {
        ns = boost::lexical_cast<OpcUa_UInt32>(matches[2]);
    }
    catch (const boost::bad_lexical_cast &exc) {
        errlogPrintf("%s getTagNode(%s) SKIP for bad namespace\n",prec->name,ItemPath);
        return false;
    }
    type = ((std::string)matches[3]).c_str()[0];
    switch(type) {
    case 's':
        sId = matches[4];
        nodeId = UaNodeId(sId.c_str(),ns);
        break;
    case 'i':
        OpcUa_UInt32 iId;
        try {
            iId = boost::lexical_cast<OpcUa_UInt32>(matches[4]);
        }
        catch (const boost::bad_lexical_cast &exc) {
            errlogPrintf("%s getTagNode(%s) SKIP for bad integer id\n",prec->name,ItemPath);
            return false;
        }
        nodeId = UaNodeId(iId,ns);
        break;
    default:
        errlogPrintf("%s getTagNode(%s) SKIP id-type not supported\n",prec->name,ItemPath);
        return false;
    }
    errlogPrintf("%s parsed to: tag:'%s' node:'%s'\n",prec->name,tag.c_str(), nodeId.toString().toUtf8());

    pSession = DevUaSession::addUaItem(tag,this);
    if(pSession == NULL) {
        errlogPrintf("%s Can' find client/subscription tag:'%s'\n",prec->name,tag.c_str());
        return false;
    }
    return true;
}

long UaItem::write(UaVariant &tempValue)
{
    stat = UaStatusCode(pSession->writeFunc(this, tempValue)).statusCode();
    if( OpcUa_IsGood(stat))
        return 0;
    return 1;
}
