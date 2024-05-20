CFLAGS= -O3 -Wall -Wextra -ggdb -I$(CURDIR)/thirdparty/ -I$(CURDIR) -I$(RAYLIB_DIR)/src
LFLAGS= -L$(RAYLIB_DIR)/src -lraylib -ldl -lm -lglfw -ldl -lpthread

BUILD_DIR=$(CURDIR)/build
SRC_DIR=$(CURDIR)/demos

SOURCES=$(wildcard $(SRC_DIR)/*.c)
EXECUTABLES=$(basename $(notdir $(SOURCES)))

RAYLIB_URL=https://github.com/raysan5/raylib
RAYLIB_TAG=4.5.0
RAYLIB_DIR=$(CURDIR)/raylib

#https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux
.PHONY: $(RAYLIB_DIR) all clean

$(RAYLIB_DIR):
	@if [ -d "$(RAYLIB_DIR)" ]; then \
		cd $(RAYLIB_DIR) && \
		git fetch origin && \
		git reset --hard origin/master && \
		cd ..; \
	else \
		git -c advice.detachedHead=false clone -b $(RAYLIB_TAG) $(RAYLIB_URL) $@; \
	fi

$(RAYLIB_DIR)/src/libraylib.a: $(RAYLIB_DIR)
	@echo "Compiling raylib"
	cd $(RAYLIB_DIR) && \
	git fetch origin && \
	git reset --hard origin/master && \
	cd ..;
	make clean
	make -C $(RAYLIB_DIR)/src -f $(RAYLIB_DIR)/src/Makefile PLATFORM=PLATFORM_DESKTOP 

raylib: $(RAYLIB_DIR)/src/libraylib.a

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

all: raylib $(addprefix $(BUILD_DIR)/, $(EXECUTABLES))  

$(BUILD_DIR)/%: $(SRC_DIR)/%.c | $(BUILD_DIR) raylib	
	gcc $(CFLAGS) -o $@ $< $(LFLAGS) $(LIBS)

$(EXECUTABLES): %: $(BUILD_DIR)/%

clean:
	rm -rf $(BUILD_DIR)

 
