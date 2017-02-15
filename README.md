# opcUaUnifiedAutomation

EPICS opcUa device support with Unified Automation C++ based
[client sdk](https://www.unified-automation.com/products/client-sdk.html).


## Build and Installation

* This module has a standard EPICS module structure. It compiles against
  recent versions of EPICS Base 3.14, 3.15 and 3.16.

* Depending on your Linux installation, install *libcrypto*.

* When cloning this module, you may create local settings that are not being
  traced by git.

  * Create *configure/RELEASE.local* and set *EPICS_BASE* to point to your
    EPICS installation.

  * Create *configure/CONFIG_SITE.local* and set *UASDK* to point to your
    Unified Automation OPC UA C++ Client SDK installation.


## Features

* Data conversion for all integer and float data types. Data loss may occur for
  conversion from float to integer types and from long to short integer types.

* In-Records support *SCAN="I/O Intr"* and all periodic scantimes.

* Out-records become bidirectional In/Out-records. They are allways inititalized
  by the server. The EPICS Out-record will be updated if the OPC item is written 
  by another device (e.g. PLC).
  
  Writing to the hardware from two sides (EPICS and the hardware covered by the 
  OPC server) at the same time can cause problems.

  For values that are frequently written by the PLC a write by the EPICS record
  will be overwritten in the opc-server by the PLC before it is sent to the PLC 
  from the opc-server. In this case a caput will not be recognized by the PLC!

  This is a behaviour due to the opc-server and PLC, no bug in the EPICS device 
  support.

  Also an EPICS write that follows immediately after a PLC write (very short time),
  may be discarded by the device support. 

* Support for the following record types:

  - ai, ao
  - bi, bo
  - mbbi, mbbo
  - mbbiDirect, mbboDirect
  - longin, longout
  - stringin, stringout
  - waveform

* LINR field of ai/ao records: 

  - LINR='NO CONVERSION': The OPC value is direct written to the VAL field.
    SMOO filter is done by the device support.
  - LINR='SLOPE': The OPC value is written to the RVAL field and the record
    conversion is performed. 

* If OROC of ao-record and SCAN is set, the record will change its output as expected.
  
* waveform: Data conversion from native OpcUa type to waveform.FTVL type is
  supported.
  
* Timestamps: Default is the EPICS timestamp. With setting TSE="-2" the timestamp
 of the server will be used.

* Server needs to be up when starting iocShell, but it will reconnect if server 
  is down for a while.

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
    drvOpcUaSetup("opc.tcp://localhost:4841","","",0,0)
    
    # With certificates and host. Certificated connection not supported now :-(
    #drvOpcUaSetup("opc.tcp://localhost:4841","/home/kuner/opcProjekt/certificates/hazel_store/certs","hazel",0,0)
    
    dbLoadRecords "db/freeopcuaTEST.db"

    OpcUaDebug(1)
    setIocLogDisable 1
    iocInit
    OpcUaStat(0)
```

## Define OpcUa Links

### Find node by BrowsePath: 

Define the path: Namespace Index followed by colon ':' and the BrowseName items 
seperated by a dot, begining after the 'Root.Objects' node.
```
  2:NewObject.MyArrayVar
```
Some servers create node Identifiers by concatenating the BrowseNames. In this case
it will be good to use the nodeId (see below) or there is the option to do 
concatenation by the driver, set with an option of the client program, parameter
on the drvOpcUaSetup() routine.

### Find node by Id:

Define the NodeId: Namespace Index followed by comma ',' and the Identifier.
```
  2,1004
  2,S7.DB_RD.stHeartbeat
```
The client tool uses the same driver as the device support and is suited to test
the server access.

## Connection types

OpcUa offers secure connections and the Unified Automation SDK supports this. It
needs a certificate store to work. Up to now the device support doesn't offer a
possibility to choose the servers endpoint, so just the anonymous connection is 
supported. The certificate store path may be empty at initialisation . This will 
be improved soon.
  
## Ioc Shell fuctions

* drvOpcuaSetup:

```
    drvOpcuaSetup("opc.tcp://SERVER:PORT","CERTIFICATE_STORE","HOST",MODE,DEBUG)

```

  - SERVER:PORT: Mandatory
  - CERTIFICATE_STORE: Optional. Not used now, just anonymous access supported
  - HOST: Optional. Neccessary if UA_GetHostname() failes.
  - MODE: How to interpret the opcUa links.
    - BOTH=0: NODEID or BROWSEPATH, mixed in access to one server - quite slow!
    - NODEID=1
    - BROWSEPATH=2
    - BROWSEPATH_CONCAT=3: Means concatenate path 'a.b.c' to 'a/a.b/a.b.c' May be usefull in some cases
  - DEBUG: Debuglevel for the support module set also with OpcUaDebug(). To debug single records set field .TPRO > 1

* opcuaDebug:

```
    opcuaDebug(debugLevel)

```

Set verbosity level of the support module. To check single records set the record.TPRO field > 1 to 
get specific debug information to this record.

* opcuaStat:

```
    opcuaStat(verbosity)

```

Show all connections.


## Release notes

R0-8-2: Initial version

R0-9: 

* Simplify the writing of iocShell functions. this needs an update of st.cmd files! 

* setup RELEASE and CONFIG_SITE to relativ paths for BASE and UASDK

R0-9-1: 

* To prevent git from tracing local definitions use configure/CONFIG_SITE.user and configure/RELEASE.user

## Known bugs

* For big amount of channels, in our test > 800, the epics will break channel access
connections after some hours. Need to restart IOC.

* In the same test drvOpcuaSetup with mode NODEID will not connect to the opcua objects 
on the server. No error Message! It works relyably but very slow with mode BOTH. In this 
mode each object does its own connection.

