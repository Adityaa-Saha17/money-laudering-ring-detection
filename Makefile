CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc

AML_BIN = aml
SRC_MAIN = src/main.cpp
SRCS = src/ingest_scc.cpp src/cycles_fan.cpp src/chains_peeling.cpp src/exact_eval.cpp src/main.cpp

.PHONY: all clean test generate detect evaluate report presentation help

all: $(AML_BIN)

$(AML_BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRC_MAIN) -o $(AML_BIN)

test: $(AML_BIN)
	./$(AML_BIN) test

generate: $(AML_BIN)
	./$(AML_BIN) generate data/synth.csv

detect: $(AML_BIN)
	@if [ -f data/HI-Small_Trans.csv ]; then \
		./$(AML_BIN) detect data/HI-Small_Trans.csv; \
	elif [ -f data/synth.csv ]; then \
		./$(AML_BIN) detect data/synth.csv; \
	else \
		echo "No dataset found in data/. Run 'make generate' first."; \
	fi

evaluate: $(AML_BIN)
	@if [ -f data/HI-Small_Trans.csv ]; then \
		./$(AML_BIN) evaluate data/HI-Small_Trans.csv; \
	elif [ -f data/synth.csv ]; then \
		./$(AML_BIN) evaluate data/synth.csv; \
	else \
		echo "No dataset found in data/. Run 'make generate' first."; \
	fi

report:
	@if command -v tectonic >/dev/null 2>&1; then \
		tectonic report/report.tex; \
	elif [ -x /opt/homebrew/bin/tectonic ]; then \
		/opt/homebrew/bin/tectonic report/report.tex; \
	else \
		echo "tectonic not found."; \
	fi

presentation:
	@if command -v tectonic >/dev/null 2>&1; then \
		tectonic presentation/presentation.tex; \
	elif [ -x /opt/homebrew/bin/tectonic ]; then \
		/opt/homebrew/bin/tectonic presentation/presentation.tex; \
	else \
		echo "tectonic not found."; \
	fi

clean:
	rm -f $(AML_BIN) aml_test_*.csv report/*.aux report/*.log report/*.out presentation/*.aux presentation/*.log presentation/*.nav presentation/*.out presentation/*.snm presentation/*.toc

help:
	@echo "Available make targets:"
	@echo "  make              - Build aml executable"
	@echo "  make test         - Run the small hand-checked tests"
	@echo "  make generate     - Generate synthetic dataset in data/synth.csv"
	@echo "  make detect       - Run pattern and densest subgraph detection on dataset"
	@echo "  make evaluate     - Compute precision/recall/F1 and scaling benchmark"
	@echo "  make report       - Compile report/report.tex to report/report.pdf"
	@echo "  make presentation - Compile presentation/presentation.tex to presentation/presentation.pdf"
	@echo "  make clean        - Remove built binary and intermediate artifacts"
