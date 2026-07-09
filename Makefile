CLANG := clang
CLANGXX := $(subst clang,clang++,$(CLANG))
LD := $(subst clang,ld.lld,$(CLANG))
OBJCOPY := $(subst clang,llvm-objcopy,$(CLANG))
OBJDUMP := $(subst clang,llvm-objdump,$(CLANG))

MUSL := $(realpath deps/musl)
MUSL_TARGET := $(MUSL)/release/include/jolt/jolt.ld
BUILTINS := $(realpath deps/builtins)
BUILTINS_TARGET := $(BUILTINS)/build/libcompiler-rt.a
LIBCXX := $(realpath deps/libcxx)
LIBCXX_TARGET := $(LIBCXX)/release/lib/libc++.a

DEBUG := false

BASE_CFLAGS := --target=riscv64 -march=rv64imac \
	-Os -g -DJOLT -mcmodel=medany \
	-fdata-sections -ffunction-sections -fvisibility=hidden \
	-DJOLT_VM_MAX_OUTPUT_SIZE=16777216

LDFLAGS := --gc-sections --static \
  --nostdlib --sysroot $(MUSL)/release \
  -L$(MUSL)/release/lib -L$(BUILTINS)/build \
  -lc -lgcc -lcompiler-rt \
  -L$(LIBCXX)/release/lib \
  -lc++ -lc++abi -lunwind \
	-T$(MUSL)/release/include/jolt/jolt.ld

# Show all linker errors
LDFLAGS += --error-limit=0

# Image presets:
#   make IMAGE=full   -> original 1200x675, 10 samples, depth 20, all spheres (very heavy)
#   make IMAGE=small  -> 80x45, fits execution on a ~24GB box (default)
#   make IMAGE=tiny   -> 16x16, small enough to PROVE on a ~24GB box
# By default small image is used.
ifeq ($(IMAGE),full)
RT_DEFS ?=
else ifeq ($(IMAGE),tiny)
RT_DEFS ?= -DRT_SMALL_SCENE -DRT_CENTER_SAMPLE -DRT_IMAGE_WIDTH=25 -DRT_SAMPLES=1 -DRT_DEPTH=3
else
RT_DEFS ?= -DRT_SMALL_SCENE -DRT_CENTER_SAMPLE -DRT_IMAGE_WIDTH=80 -DRT_SAMPLES=1 -DRT_DEPTH=5
endif

CXXFLAGS := \
  -Wall -Werror \
  -std=c++20 \
  -fno-exceptions \
  -D_GNU_SOURCE \
  -nostdinc -nostdinc++ \
  -isystem $(LIBCXX)/release/include/c++/v1 \
  -isystem $(MUSL)/release/include \
  $(BASE_CFLAGS) $(RT_DEFS)
ifneq (true,$(DEBUG))
	CXXFLAGS += -DNO_DEBUG_INFO
endif

all: run-jolt

build-jolt: $(MUSL_TARGET) $(BUILTINS_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) $(CXXFLAGS) tracer_src/main.cc -c -o build/raytracer.o
	$(LD) $(LDFLAGS) build/raytracer.o -o build/raytracer
	$(OBJDUMP) -d build/raytracer > build/raytracer_dump.txt

# Native reference build (host clang++, no JOLT) for generating image.ppm.
# -ffp-contract=off matches jolt's soft-float (no FMA) so the two are bit-identical
# given deterministic sampling (RT_CENTER_SAMPLE) and no random spheres (RT_SMALL_SCENE).
NATIVE_CXXFLAGS := -std=c++20 -O2 -ffp-contract=off $(RT_DEFS)

build-native:
	$(CLANGXX) $(NATIVE_CXXFLAGS) tracer_src/main.cc -o build/raytracer_native

run-native: build-native
	./build/raytracer_native > image.ppm
	@echo "wrote image.ppm ($$(wc -c < image.ppm) bytes)"

# Runs the binary from the project root so ./build/raytracer and image_jolt.ppm
# resolve consistently with run-native (cargo run would CWD into runner/).
run-jolt: build-jolt
	cd runner && cargo build --release
	./runner/target/release/jolt-vm-runner

# Prove + verify. Builds the guest (via build-jolt) then runs the prover.
# Proving is RAM-heavy (~2GB per million cycles): IMAGE=tiny is the safe default.
# A non-tiny image warns and prompts; FORCE=true skips the prompt.
prove-jolt: build-jolt
	@if [ "$(IMAGE)" != "tiny" ] && [ "$(FORCE)" != "true" ]; then \
		echo "WARNING: proving IMAGE=$(or $(IMAGE),small) is heavy and may OOM on a 24GB box."; \
		echo "         use 'make prove-jolt IMAGE=tiny' for a safe proof, or FORCE=true to proceed."; \
		read -r -p "Proceed with IMAGE=$(or $(IMAGE),small)? [y/N] " ans; \
		case "$$ans" in y|Y) ;; *) echo "aborted"; exit 1 ;; esac; \
	fi
	cd runner && cargo build --release
	./runner/target/release/jolt-vm-runner prove

MUSL_CFLAGS := $(BASE_CFLAGS) -DPAGE_SIZE=4096
# ifneq (true,$(DEBUG))
# 	MUSL_CFLAGS += -DCKB_MUSL_DUMMIFY_PRINTF
# endif

$(MUSL_TARGET):
	cd $(MUSL) && \
		CLANG=$(CLANG) \
			BASE_CFLAGS="$(MUSL_CFLAGS)" \
			./ckb/build.sh

BUILTINS_CFLAGS := --target=riscv64  -march=rv64imac -mabi=lp64 -mcmodel=medany
BUILTINS_CFLAGS += -nostdinc -I $(MUSL)/release/include -I $(LIBCXX)/release/include
BUILTINS_CFLAGS += -Os -g
BUILTINS_CFLAGS += -fdata-sections -ffunction-sections -fno-builtin -fvisibility=hidden -fomit-frame-pointer
BUILTINS_CFLAGS += -I compiler-rt/lib/builtins
BUILTINS_CFLAGS += -DVISIBILITY_HIDDEN -DCOMPILER_RT_HAS_FLOAT16

$(BUILTINS_TARGET): $(MUSL_TARGET) $(LIBCXX_TARGET)
	cd $(BUILTINS) && \
		make CC=$(CLANG) \
			AR=$(subst clang,llvm-ar,$(CLANG)) \
			CFLAGS="$(BUILTINS_CFLAGS)"

LLVM_CMAKE_OPTIONS := -DCMAKE_BUILD_TYPE=MinSizeRel
# LLVM_CMAKE_OPTIONS += -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF -DLIBCXX_ENABLE_UNICODE=OFF
LLVM_CMAKE_OPTIONS += -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF
LLVM_CMAKE_OPTIONS += -DLIBCXXABI_SILENT_TERMINATE=ON

$(LIBCXX_TARGET): $(MUSL_TARGET)
	cd $(LIBCXX) && \
		CLANG=$(CLANG) \
			BASE_CFLAGS="$(BASE_CFLAGS)" \
			MUSL=$(MUSL)/release \
			DEBUG=true \
			LLVM_VERSION="18.1.8" \
			LLVM_PATCH="$(realpath llvm_patch)" \
			LLVM_CMAKE_OPTIONS="$(LLVM_CMAKE_OPTIONS)" \
		  ./build.sh
	touch $@

clean:
	rm -rf build/*
	cd $(MUSL) && make clean && rm -rf release
	cd $(BUILTINS) && make clean
	cd $(LIBCXX) && rm -rf release build llvm_src

.PHONY: all build-jolt build-native run-native run-jolt prove-jolt clean
