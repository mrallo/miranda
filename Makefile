BIN=usr/local/bin
DST=dist
LIB=usr/local/lib
MAN=usr/local/man/man1

CC = gcc -w
CFLAGS =
EX =
YACC = byacc

OBJS = big.o cmbnms.o data.o lex.o reduce.o steer.o trans.o types.o utf8.o y.tab.o

all: mira miralib/menudriver exfiles

mira: $(OBJS) version.c miralib/.version fdate .host Makefile
	$(CC) $(CFLAGS) -DVERS=`cat miralib/.version` -DVDATE="\"`./revdate`\"" \
        -DHOST="`./quotehostinfo`" version.c $(OBJS) -lm -o mira
	strip mira$(EX)

y.tab.c y.tab.h: rules.y
	$(YACC) -d rules.y

$(OBJS): data.h combs.h utf8.h y.tab.h Makefile

data.o: .xversion
big.o data.o lex.o reduce.o steer.o trans.o types.o: big.h
big.o data.o lex.o reduce.o steer.o rules.y types.o: lex.h

utf8.o: utf8.h Makefile
cmbnms.o: cmbnms.c Makefile

cmbnms.c combs.h: gencdecs
	./gencdecs

miralib/menudriver: menudriver.c Makefile
	$(CC) $(CFLAGS) menudriver.c -o miralib/menudriver
	chmod 755 miralib/menudriver$(EX)
	strip miralib/menudriver$(EX)

tellcc:
	@echo $(CC) $(CFLAGS)

cleanup:
	-rm -rf *.o fdate miralib/menudriver mira$(EX) $(DST)
	./unprotect
	-rm -f miralib/preludx miralib/stdenv.x miralib/ex/*.x

clean: cleanup

.host:
	./hostinfo > .host

install: all
	cp mira$(EX) /$(BIN)
	cp mira.1 /$(MAN)
	rm -rf /$(LIB)/miralib
	./protect
	cp -pPR miralib /$(LIB)/miralib
	./unprotect
	find /$(LIB)/miralib -exec chown `./ugroot` {} \;

release: all
	-rm -rf usr
	mkdir -p $(BIN) $(DST) $(LIB) $(MAN)
	cp mira$(EX) $(BIN)
	cp mira.1 $(MAN)
	cp -pPR miralib $(LIB)/miralib
	tar czf $(DST)/miranda-`./mira -v`.tgz ./usr
	-rm -rf usr

SOURCES = .xversion big.c big.h gencdecs data.h data.c lex.h lex.c reduce.c rules.y \
          steer.c trans.c types.c utf8.h utf8.c version.c fdate.c

sources: $(SOURCES)
	@echo $(SOURCES)

exfiles:
	@-./mira -make -lib miralib ex/*.m

mira.1.html: mira.1 Makefile
	man2html mira.1 | sed '/Return to Main/d' > mira.1.html
