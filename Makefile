CFLAGS= -O3 -Wall -Wextra -ggdb -I$(CURDIR)/thirdparty/ -I$(CURDIR) -I$(RAYLIB_DIR)/src
LFLAGS= -L$(RAYLIB_DIR)/src -lraylib -ldl -lm 

BUILD_DIR=$(CURDIR)/build
SRC_DIR=$(CURDIR)/demos

SOURCES=$(wildcard $(SRC_DIR)/*.c)

RAYLIB_URL=https://github.com/raysan5/raylib
RAYLIB_TAG=4.5.0
RAYLIB_DIR=$(CURDIR)/raylib

#https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux
.PHONY: $(RAYLIB_DIR) all clean

$(RAYLIB_DIR):
	@echo "Raylib dir"
	@if [ -d "$(RAYLIB_DIR)" ]; then \
		cd $(RAYLIB_DIR) && \
		git fetch origin && \
		git reset --hard origin/master && \
		cd ..; \
	else \
	    @echo "cloning" \
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

all: raylib  $(BUILD_DIR) img2nn shape xor adder layout opengl_matrix_mul #matrix_mul

img2nn: $(SRC_DIR)/img2nn.c | $(BUILD_DIR)
	gcc $(CFLAGS) -o $(BUILD_DIR)/$@ $< $(LFLAGS)  

shape: $(SRC_DIR)/shape.c | $(BUILD_DIR)
	gcc $(CFLAGS) -o $(BUILD_DIR)/$@ $< $(LFLAGS)  

xor: $(SRC_DIR)/xor.c | $(BUILD_DIR)
	gcc $(CFLAGS) -o $(BUILD_DIR)/$@ $< $(LFLAGS)  

adder: $(SRC_DIR)/adder.c | $(BUILD_DIR)
	gcc $(CFLAGS) -o $(BUILD_DIR)/$@ $< $(LFLAGS)  

layout: $(SRC_DIR)/layout.c | $(BUILD_DIR)
	gcc $(CFLAGS) -o $(BUILD_DIR)/$@ $< $(LFLAGS)  

opengl_matrix_mul: $(SRC_DIR)/opengl_matrix_mul.c | $(BUILD_DIR)
	gcc $(CFLAGS) -o $(BUILD_DIR)/$@ $< $(LFLAGS) -lglfw -ldl -lpthread -lGL -lGLEW -lglut   -DNO_PRINT_MAT
 
 

$(EXECUTABLES): %: $(BUILD_DIR)/%

clean:
	rm -rf $(BUILD_DIR)

 