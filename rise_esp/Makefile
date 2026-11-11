CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude -I.
ESP_SRC = src/esp.cpp
MEM_SRC = src/memory.cpp
GAME_SRC = game/rise_reader.cpp

all: rise_esp rise_esp_test test_mem

rise_esp: src/main.cpp $(ESP_SRC) $(MEM_SRC) $(GAME_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

rise_esp_test: tests/test_math.cpp $(ESP_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_mem: tests/test_mem.cpp $(MEM_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

check-ro:
	sh tools/check_readonly.sh

test: check-ro rise_esp_test test_mem
	./rise_esp_test
	./test_mem

clean:
	rm -f rise_esp rise_esp_test test_mem

.PHONY: all test clean check-ro
