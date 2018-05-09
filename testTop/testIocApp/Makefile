TOP=..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

DB = testRecords.db

IOCS += OPCUAIOC TESTIOC

PROD_IOC = $(IOCS)
DBD = $(IOCS:%=%.dbd)

TESTSCRIPTS = testRecords.t

OPCUAIOC_SRCS +=OPCUAIOC_registerRecordDeviceDriver.cpp SoftIocMain.cpp 
TESTIOC_SRCS += TESTIOC_registerRecordDeviceDriver.cpp testIocMain.cpp 

ifeq ($(UASDK_DEPLOY_MODE),PROVIDED)
define UA_template
  $(1)_DIR = $(UASDK_DIR)
endef
$(foreach lib, $(UASDK_LIBS), $(eval $(call UA_template,$(lib))))
endif

# for use CA.pm
PERL = LD_LIBRARY_PATH=$(EPICS_BASE_LIB) PERL5LIB=$(EPICS_BASE)/lib/perl perl -CSD

INSTALL_BOOT =$(TOP)/iocBoot
STCMD_INSTALLS = $(IOCS:%=$(INSTALL_BOOT)/ioc%/st.cmd)

#----------------------------- testServer -----------------------------#

PROD_HOST += testServer


testServer_CPPFLAGS += -std=c++11 -ggdb
testServer_CXXFLAGS += -pthread -fomit-frame-pointer
testServer_SYS_LIBS += dl pthread xml2 boost_system 
testServer_SYS_LIBS += opcuaserver opcuaprotocol opcuacore
testServer_LDFLAGS  += -L$(FREEOPCUA)/.libs

testServer_INCLUDES += -I$(FREEOPCUA)/include
testServer_INCLUDES += -I/usr/include/libxml2
testServer_LDFLAGS += '-Wl,-rpath,$(FREEOPCUA)/.libs'

#-----------------------------OPCUAIOC -----------------------------#

OPCUAIOC_DBD += base.dbd 
OPCUAIOC_DBD += opcUa.dbd testBtbl.dbd 

OPCUAIOC_LIBS += opcUa
OPCUAIOC_LIBS += $(UASDK_LIBS)
OPCUAIOC_LIBS += $(EPICS_BASE_IOC_LIBS)

OPCUAIOC_SYS_LIBS_Linux += xml2 crypto
OPCUAIOC_SYS_LIBS += boost_regex

USR_LDFLAGS_WIN32 += /LIBPATH:$(UASDK)/third-party/win32/vs2010sp1/libxml2/out32dll
USR_LDFLAGS_WIN32 += /LIBPATH:$(UASDK)/third-party/win32/vs2010sp1/openssl/out32dll 

OPCUAIOC_SYS_LIBS_WIN32 += ws2_32 advapi32 user32 ssleay32 libeay32 crypt32 libxml2 ole32 oleaut32 strmiids

#-----------------------------TESTIOC -----------------------------#

TESTIOC_DBD += base.dbd 
TESTIOC_DBD += opcUa.dbd testBtbl.dbd 

TESTIOC_LIBS += opcUa
TESTIOC_LIBS += $(UASDK_LIBS)
TESTIOC_LIBS += $(EPICS_BASE_IOC_LIBS)

TESTIOC_SYS_LIBS_Linux += xml2 crypto
TESTIOC_SYS_LIBS += boost_regex

USR_LDFLAGS_WIN32 += /LIBPATH:$(UASDK)/third-party/win32/vs2010sp1/libxml2/out32dll
USR_LDFLAGS_WIN32 += /LIBPATH:$(UASDK)/third-party/win32/vs2010sp1/openssl/out32dll 

TESTIOC_SYS_LIBS_WIN32 += ws2_32 advapi32 user32 ssleay32 libeay32 crypt32 libxml2 ole32 oleaut32 strmiids


include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

build: $(STCMD_INSTALLS)

# install generated st.cmd files
$(STCMD_INSTALLS): $(INSTALL_BOOT)/ioc%/st.cmd: ../st.cmd.%
	$(ECHO) "Installing $@"
	@$(INSTALL) -d -m 755 $< $(@D)
	@$(MV) $(@D)/st.cmd.$* $@

