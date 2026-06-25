CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2
CPPFLAGS ?= -Iinclude

KERNELS := \
	haystack_enns_flat \
	haystack_enns_filtered \
	haystack_bm25 \
	haystack_hybrid_merge \
	haystack_context_pack \
	haystack_lexrank

BINS := $(addprefix build/,$(KERNELS))

.PHONY: all clean test cgra-check cgra-clean

all: $(BINS)

build:
	mkdir -p build

build/%: src/%.c include/bench_config.h include/bench_common.h include/topk.h include/fixed_point.h include/checksum.h | build
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@

test: all
	bash tests/run_all.sh
	bash tests/check_outputs.sh

cgra-check:
	bash tests/check_cgra_shape.sh
	bash tests/check_cgra_behavior.sh
	bash scripts/count_instructions.sh

cgra-clean:
	rm -rf build/cgra

clean:
	rm -rf build
