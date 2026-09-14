CC ?= cc
PYTHON ?= python3
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude

.PHONY: all test test-python sanitize library viewer clean

all: test_ecg_detector

test_ecg_detector: src/ecg_detector.c tests/test_ecg_detector.c include/ecg_detector.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/ecg_detector.c tests/test_ecg_detector.c -lm -o $@

test: test_ecg_detector
	./test_ecg_detector

test-python: library
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

library:
	mkdir -p build
	$(CC) -dynamiclib $(CPPFLAGS) $(CFLAGS) src/ecg_detector.c \
		src/ecg_detector_ffi.c -lm -o build/libecg_detector.dylib

viewer: library
	$(PYTHON) -m ecg_viewer

sanitize:
	$(CC) $(CPPFLAGS) -std=c99 -O1 -g -Wall -Wextra -Wpedantic \
		-fsanitize=address,undefined src/ecg_detector.c \
		tests/test_ecg_detector.c -lm -o test_ecg_detector_san
	./test_ecg_detector_san

clean:
	rm -f test_ecg_detector test_ecg_detector_san
	rm -rf test_ecg_detector_san.dSYM
	rm -rf build
