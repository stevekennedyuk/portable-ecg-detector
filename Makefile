CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude

.PHONY: all test sanitize clean

all: test_ecg_detector

test_ecg_detector: src/ecg_detector.c tests/test_ecg_detector.c include/ecg_detector.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/ecg_detector.c tests/test_ecg_detector.c -lm -o $@

test: test_ecg_detector
	./test_ecg_detector

sanitize:
	$(CC) $(CPPFLAGS) -std=c99 -O1 -g -Wall -Wextra -Wpedantic \
		-fsanitize=address,undefined src/ecg_detector.c \
		tests/test_ecg_detector.c -lm -o test_ecg_detector_san
	./test_ecg_detector_san

clean:
	rm -f test_ecg_detector test_ecg_detector_san
	rm -rf test_ecg_detector_san.dSYM
