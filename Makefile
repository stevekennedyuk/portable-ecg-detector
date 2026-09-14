CC ?= cc
PYTHON ?= python3
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SHARED_FLAGS := -dynamiclib
else
SHARED_FLAGS := -shared -fPIC
endif

.PHONY: all test test-python sanitize strict library viewer clean

all: test_ecg_detector

test_ecg_detector: src/ecg_detector.c tests/test_ecg_detector.c include/ecg_detector.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/ecg_detector.c tests/test_ecg_detector.c -lm -o $@

test: test_ecg_detector
	./test_ecg_detector

test-python: library
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

library:
	mkdir -p build
	$(CC) $(SHARED_FLAGS) $(CPPFLAGS) $(CFLAGS) src/ecg_detector.c \
		src/ecg_detector_ffi.c -lm -o build/libecg_detector.dylib

viewer: library
	$(PYTHON) -m ecg_viewer

sanitize:
	$(CC) $(CPPFLAGS) -std=c99 -O1 -g -Wall -Wextra -Wpedantic \
		-fsanitize=address,undefined src/ecg_detector.c \
		tests/test_ecg_detector.c -lm -o test_ecg_detector_san
	./test_ecg_detector_san

strict:
	$(CC) $(CPPFLAGS) -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror \
		-Wshadow -Wconversion -Wdouble-promotion src/ecg_detector.c \
		tests/test_ecg_detector.c -lm -o test_ecg_detector_strict
	./test_ecg_detector_strict

clean:
	rm -f test_ecg_detector test_ecg_detector_san
	rm -f test_ecg_detector_strict
	rm -rf test_ecg_detector_san.dSYM
	rm -rf build
