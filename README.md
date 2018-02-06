# opcUaUnifiedAutomation

EPICS OPC UA device support using the Unified Automation C++ based
[client sdk](https://www.unified-automation.com/products/client-sdk.html).

## Prerequisites

* Unified Automation C++ Based OPC UA Client SDK.
  This device support has been developed using the 1.5.x series.

* If you want to use crypto support (authentication/encryption), you need
  libcrypto on your system - both when compiling the SDK and when generating
  any binaries (IOCs).
  The name of the package you have to install depends on the Linux distro:
  openssl-devel on RedHat/CentOS/Fedora, libssl-dev on Debian/Ubuntu.
  Use the CONFIG_SITE.local file (see below) where the binary is created
  to set this option.

* The Boost library and header files.
  The name of the package you have to install depends on the Linux distro:
  boost-devel on RedHat/CentOS/Fedora, libboost-all-dev on Debian/Ubuntu.

* If you want support for XML definitions (e.g. for using the SDK examples
  and tools), you need libxml2 an your system - both when compiling the SDK
  and when generating any binaries (IOCs).
  The name of the package you have to install depends on the Linux distro:
  libxml2-devel on RedHat/CentOS/Fedora, libxml2-dev on Debian/Ubuntu.
  Use the CONFIG_SITE.local file (see below) where the binary is created
  to set this option.

## Build and Installation

* This module has a standard EPICS module structure. It compiles against
  recent versions of EPICS Base 3.14, 3.15 and 3.16.

* When cloning this module from the repository, you may create local settings
  that are not being traced by git and don't create conflicts:

  * Create *configure/RELEASE.local* and set *EPICS_BASE* to point to your
    EPICS installation.

  * Create *configure/CONFIG_SITE.local* and set *UASDK* to point to your
    Unified Automation C++ OPC UA Client SDK installation.
    This is also where you select how the SDK libraries will be installed
    on your target systems, and the optional support choices (see above).

## Features

* Data conversion for all integer and float data types. Data loss may occur for
  conversion from float to integer types and from long to short integer types.

* In-records support *SCAN="I/O Intr"* and periodic scanning.

* Out-records become bidirectional. They are always inititalized by reading from
  the server. The EPICS Out-record will be updated if the OPC item is written
  by another device (e.g. PLC, another OPC client, local HMI).
  
  In OPC UA, the server always sits on top of the hardware that it interfaces,
  and is written to (or updated) by both the hardware below and remote clients.

  Writing to the OPC UA server from two sides (EPICS and the hardware below)
  at almost the same time can cause race conditions and lead to problems.

  For values that are frequently updated by the hardware,
  a remote write from the EPICS record may be overwritten in the OPC server by
  the hardware before the OPC server has sent it out to the hardware.
  In this case the remote write from EPICS will not reach the hardware.

  This behaviour is defined inside the OPC server that must handle such race
  conditions. The client cannot directly influence it, and such ignored writes
  are not caused by a bug in the EPICS device support.

  Also an EPICS write that follows immediately after a PLC write may be
  discarded by the device support.

  As the out-record is always updated with changes from the OPC UA server,
  it should properly reflect the status of the server at all times.

* Support for the following record types:

  - ai, ao
  - bi, bo
  - mbbi, mbbo
  - mbbiDirect, mbboDirect
  - longin, longout
  - stringin, stringout
  - waveform

* LINR field of ai/ao records: 

  - LINR="NO CONVERSION": The OPC value is direct written to (taken from)
    the VAL field, SMOO filtering is done by the device support.
  - LINR="SLOPE": The OPC value is written to (taken from) the RVAL field,
    conversion is performed by the record.

* If SCAN and OROC of an ao record are set, the record will change its output
  as expected.
  
* Waveform: Data conversion from native OpcUa type to the waveform record's
  FTVL type is supported.
  
* Timestamps: When setting TSE="-2" the OPC UA server timestamp is used.

* Initial connection and reconnection are handled appropriately.
  The retry interval for the initial connection can be set using the variable
  `drvOpcua_AutoConnectInterval` (double), the default is 10.0 [sec].

* Configurable publish interval setting.
  The default publish interval setting [ms] for the OPC UA subscriptions
  can be configured using the variable `drvOpcua_DefaultPublishInterval`
  (double), which defaults to 100.0 [ms].

* Configurable sampling interval setting.
  The sampling interval can be configured for each record by adding
  an info item like
     `info(opcua:SAMPLING, "100.0")`
  with a double value in [ms].
  The default sampling interval setting [ms] for the OPC UA monitored items
  can be configured using the variable `drvOpcua_DefaultSamplingInterval`
  (double), which defaults to -1.0 (use publishing interval). Use a setting of
  0.0 for the fastest practical rate (server defined).

* Configurable queue size setting.
  The server side queue size can be configured for each record by adding
  an info item like
     `info(opcua:QSIZE, "10")`
  with an unsigned integer value > 0.
  The default queue size setting for the OPC UA monitored items
  can be configured using the variable `drvOpcua_DefaultQueueSize` (integer),
  which defaults to 1 (no queueing).

* Configurable discard policy setting.
  The discard policy for the server side queue (in case of overrun) can be
  configured for each record requesting  to discard the oldest or the newest
  value by adding an info item like
     `info(opcua:DISCARD, "new")`
  with "old" (discard the oldest value) or "new" (discard the newest value).
  The default discard policy can be configured using the variable
  `drvOpcua_DefaultDiscardOldest` (integer),
  which defaults to 1 (discard the oldest value).

## EPICS Database Examples:

```
# I/O Interrupt, use server timestamp, path access
record(mbbiDirect,"REC:rdBits"){
        field(SCAN,"I/O Intr")
        field(TSE, "-2")
        field(DTYP,"OPCUA")
        field(INP,"@2:NewObject.MyVariable")
}

# 1 second scan, slope conversion, numeric node access
record(ai,"REC:rdVariable"){
        field(SCAN,"1 second")
        field(DTYP,"OPCUA")
        field(DISS,"INVALID")
        field(INP,"@2,1004")
        field(LINR,"SLOPE")
        field(ASLO,"0.01")
        field(AOFF,"0")
}

# waveform with string node access
record(waveform,"REC:rdIntArr"){
        field(DTYP,"OPCUA")
        field(NELM,"5")
        field(FTVL,"LONG")
        field(SCAN,"I/O Intr")
        field(DISS,"INVALID")
        field(INP,"@2,NewObject.MyArrayVar")
}

record(ao,"REC:setProp"){
        field(TSE, "-2")
        field(DTYP,"OPCUA")
        field(DISS,"INVALID")
        field(OUT,"@2:NewObject.MyProperty")
        field(LINR,"SLOPE")
        field(ASLO,"0.1")
        field(AOFF,"1")
        field(OROC,"0.5")
        field(LOPR,"-100")
        field(HOPR,"100")
}
```

## IOC setup Example

* st.cmd file:

```
    #!../../bin/linux-x86_64/OPCUAIOC

    cd ../..
    epicsEnvSet IOC OPCUAIOC
    dbLoadDatabase "dbd/OPCUAIOC.dbd",0,0
    ${IOC}_registerRecordDeviceDriver pdbbase

    # anonymous
    drvOpcuaSetup("opc.tcp://localhost:4841","","",0,0)
    
    # With certificates and host. Certificated connection not supported now :-(
    #drvOpcuaSetup("opc.tcp://localhost:4841","/home/user/certificates/cert_store/certs","localhost",0,0)
    
    dbLoadRecords "db/freeopcuaTEST.db"

    opcuaDebug(1)
    setIocLogDisable 1
    iocInit
    opcuaStat(0)
```

## Define OpcUa Links

### Find node by BrowsePath: 

Define the path: Namespace Index followed by colon ':' and the BrowseName items 
seperated by a dot, begining after the 'Root.Objects' node.
```
  2:NewObject.MyArrayVar
```
Some servers create node Identifiers by concatenating the BrowseNames. In this case
it will be good to use the nodeId (see below). There is also the option to do that
concatenation by the driver, which can be set with a parameter of `drvOpcuaSetup()`.

### Find node by Id:

Define the NodeId: Namespace Index followed by comma ',' and the Identifier.
```
  2,1004
  2,S7.DB_RD.stHeartbeat
```
The client tool uses the same driver as the device support and is suited to test
the server access.

## Connection types

OPC UA offers secure connections, which is supported by the Unified Automation SDK,
requiring a certificate store to work. Currently the device support only supports
the anonymous connection. The certificate store path parameter may be left empty
at initialisation.
  
## Ioc Shell functions

* drvOpcuaSetup:

```
    drvOpcuaSetup("opc.tcp://SERVER:PORT","CERTIFICATE_STORE","HOST",DEBUG)

```

Set up connection to OPC UA server.

  - SERVER:PORT: Mandatory
  - CERTIFICATE_STORE: Optional. Not used now, just anonymous access supported
  - HOST: Optional. Neccessary if UA_GetHostname() failes.
  - DEBUG: Debuglevel for the support module set also with OpcUaDebug(). To debug single records set field .TPRO > 1

* opcuaDebug:

```
    opcuaDebug(debugLevel)

```

Set verbosity level of the support module. 0 means quiet, just some realy serious 
errors concerning the connection. Recomended is 1 for meaningful errors, debug=2..4 
is real debug info.

To check single records set the record.TPRO field > 1 to get specific error and debug 
information to this record.

* opcuaStat:

```
    opcuaStat(verbosity)

```

Show all connections.

## Release notes

R0-8-2: Initial version

* Known bugs

  1 For a big number of channels, in our test > 800, EPICS will break channel access
    connections after some hours. Need to restart IOC.
  2 In the same test, drvOpcuaSetup with mode NODEID will not connect to the OPC UA items
    on the server. No error Message! It works reliably but very slow with mode BOTH. In this
    mode, each object does its own connection.
  3 drvOpcuaSetup with mode NODEID may not connect to many opcua objects on the 
    server. Tested with Softing Server: 150 items ok, 880 not. No error Message! 
    It works relyably but very slow with mode BOTH. In this mode each object does 
    its own connection - very slow.

R0-9: Works stable

  - Do bugfixes
  - NEW: Build modes

R0-9-1

  - NEW: Windows support

R0-9-2

  - Get chunks of items to speedup connection establishment
  - Remove parameter 'mode' in drv
  
* Please refer to the [issue tracker](https://github.com/bkuner/opcUaUnifiedAutomation/issues)
  for more details and current status of bugs and issues.
