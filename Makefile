all: debug release

build/debug:
	cmake -B build/debug                       \
	      -DCMAKE_TOOLCHAIN_FILE=lpc4370.cmake \
              -DCMAKE_BUILD_TYPE=Debug             \

debug: build/debug
	cmake --build build/debug

build/release:
	cmake -B build/release                     \
	      -DCMAKE_TOOLCHAIN_FILE=lpc4370.cmake \
              -DCMAKE_BUILD_TYPE=Release

release: build/release
	cmake --build build/release

update-images: all
	cp -p build/debug/src/pg2sdr.bin images/pg2sdr-debug.bin
	cp -p build/debug/src/pg2sdr-airspy.bin images/pg2sdr-airspy-debug.bin
	cp -p build/release/src/pg2sdr.bin images/pg2sdr.bin
	cp -p build/release/src/pg2sdr-airspy.bin images/pg2sdr-airspy.bin

clean:
	rm -fr build

.PHONY: all debug release update-images clean
