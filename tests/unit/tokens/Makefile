include ../../../config.mak

INCLUDES=-I../../..
BUILDDIR=../../../build
CFLAGS=-Wall -g -DUNIT_TEST $(INCLUDES)
LD_FLAGS=-L../../../build

default:
	$(CC) -o tokens_test tokens_test.c \
          ../../../build/common/macros.o \
          ../../../build/common/memory_pool.o \
          ../../../build/common/print_error.o \
          ../../../build/common/symbols.o \
          ../../../build/common/tokens.o \
	  $(CFLAGS)

clean:
	@rm -f tokens_test
	@echo "Clean!"

