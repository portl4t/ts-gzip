.SUFFIXES: .c .o .lo

COMPILE = $(CC) -Wall -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -g
INC_PATH = -Imodules
LIB_PATH = -L/usr/local/lib -lz

SHARED_OBJS = ts_gzip.lo


ALL_OBJS = $(SHARED_OBJS)

ALL_PRGS = 
ALL_LIBS = libtsgzip.so

all: $(ALL_OBJS) $(ALL_PRGS) $(ALL_LIBS)

libtsgzip.so:
	$(COMPILE) -o $@ $< -shared $(SHARED_OBJS) $(LIB_PATH)
.c:
	$(COMPILE) -o $@ $< $(SHARED_OBJS) $(LIB_PATH) $(INC_PATH)
.c.o:
	$(COMPILE) -c -o $@ $<  $(INC_PATH)
.c.lo:
	$(COMPILE) -c -fPIC -o $@ $<  $(INC_PATH)
install:
	/bin/cp -f $(ALL_LIBS) /usr/lib64/
	mkdir -p /usr/include/ts_gzip/
	/bin/cp -f *.h /usr/include/ts_gzip/
clean:
	rm -f $(ALL_OBJS) $(ALL_PRGS) $(ALL_LIBS)

