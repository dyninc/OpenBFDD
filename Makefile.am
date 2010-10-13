bin_PROGRAMS = bfdd-beacon bfdd-control

AM_CXXFLAGS = $(INTI_CFLAGS) $(WARNINGCXXFLAGS) $(OTHERCXXFLAGS)

COMMON_INC = common.h utils.h log.h SmartPointer.h threads.h bfd.h standard.h
COMMON_SRC = $(COMMON_INC) common.cpp utils.cpp log.cpp SmartPointer.cpp threads.cpp bfd.cpp
CONTROL_SRC = bfdd-control.cpp
BEACON_INC = Beacon.h CommandProcessor.h Scheduler.h Session.h
BEACON_SRC = $(BEACON_INC) BeaconMain.cpp Beacon.cpp CommandProcessor.cpp Scheduler.cpp Session.cpp

bfdd_beacon_SOURCES = $(COMMON_SRC) $(BEACON_SRC)
bfdd_beacon_LDADD =  $(INTI_LIBS)  
bfdd_beacon_LDFLAGS = -pthread
bfdd_beacon_MANS = bfdd-beacon.8

bfdd_control_SOURCES = $(COMMON_SRC) $(CONTROL_SRC)
bfdd_control_LDADD =  $(INTI_LIBS)  
bfdd_control_MANS = bfdd-control.8 

EXTRA_DIST = $(bfdd_beacon_MANS) $(bfdd_control_MANS) DEVELOPERS LICENSE
man_MANS = $(bfdd_beacon_MANS) $(bfdd_control_MANS)





