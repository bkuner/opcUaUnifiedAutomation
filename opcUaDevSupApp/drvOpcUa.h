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

#ifndef __DRVOPCUA_H
#define __DRVOPCUA_H

// toolbox header

#include <devOpcUa.h>

#ifdef __cplusplus
extern "C" {
#endif
    typedef enum {BOTH=0,NODEID,BROWSEPATH,BROWSEPATH_CONCAT,GETNODEMODEMAX} GetNodeMode;
    const char *variantTypeStrings(int type);
    extern long opcUa_close(int verbose);
    extern long OpcUaSetupMonitors(void);
    extern long opcUa_io_report (int); /* Write IO report output to stdout. */
    extern void addOPCUA_Item(OPCUA_ItemINFO *h);
    extern void signalHandler( int signum );
// iocShell:
    extern long OpcUaWriteItems(OPCUA_ItemINFO* pOU_ItemINFO);
// client:
    extern long OpcReadValues(int verbose,int monitored);
    extern long OpcWriteValue(int opcUaItemIndex,double val,int verbose);
#ifdef __cplusplus
}
    extern long opcUa_init(UaString &g_serverUrl, UaString &g_applicationCertificate, UaString &g_applicationPrivateKey, UaString &nodeName, GetNodeMode mode, int verbose);
#endif

#endif /* ifndef __DRVOPCUA_H */
