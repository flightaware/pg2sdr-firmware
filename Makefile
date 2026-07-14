all: pg2sdr-debug pg2sdr-release airspymini-debug airspymini-release

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

airspymini-debug:
	cmake -B build/airspymini-debug            \
	      -DCMAKE_BUILD_TYPE=Debug             \
	      -DTARGET_HARDWARE=AIRSPYMINI
	cmake --build build/airspymini-debug
	mkdir -p images; cp build/airspymini-debug/airspymini-firmware-*.bin images/

airspymini-release:
	cmake -B build/airspymini-release          \
	      -DCMAKE_BUILD_TYPE=Release           \
	      -DTARGET_HARDWARE=AIRSPYMINI
	cmake --build build/airspymini-release
	mkdir -p images; cp build/airspymini-release/airspymini-firmware-*.bin images/

clean:
	rm -fr build images

.PHONY: all pg2sdr-debug pg2sdr-release airspymini-debug airspymini-release clean
