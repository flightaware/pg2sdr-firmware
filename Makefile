all: pg2sdr-debug pg2sdr-release airspy-debug airspy-release

pg2sdr-debug:
	cmake -B build/pg2sdr-debug                \
	      -DCMAKE_BUILD_TYPE=Debug             \
	      -DTARGET_HARDWARE=PG2SDR
	cmake --build build/pg2sdr-debug
	mkdir -p images; cp build/pg2sdr-debug/pg2sdr-firmware-*.bin images/

pg2sdr-release:
	cmake -B build/pg2sdr-release              \
	      -DCMAKE_BUILD_TYPE=Release           \
	      -DTARGET_HARDWARE=PG2SDR
	cmake --build build/pg2sdr-release
	mkdir -p images; cp build/pg2sdr-release/pg2sdr-firmware-*.bin images/

airspy-debug:
	cmake -B build/airspy-debug                \
	      -DCMAKE_BUILD_TYPE=Debug             \
	      -DTARGET_HARDWARE=AIRSPY
	cmake --build build/airspy-debug
	mkdir -p images; cp build/airspy-debug/airspy-firmware-*.bin images/

airspy-release:
	cmake -B build/airspy-release              \
	      -DCMAKE_BUILD_TYPE=Release           \
	      -DTARGET_HARDWARE=AIRSPY
	cmake --build build/airspy-release
	mkdir -p images; cp build/airspy-release/airspy-firmware-*.bin images/

clean:
	rm -fr build images

.PHONY: all pg2sdr-debug pg2sdr-release airspy-debug airspy-release clean
