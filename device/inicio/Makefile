# Tela inicial (LVGL) para o iPhone. Usa o compilador cruzado host/iphone-cc
# (sysroot do scripts/10-toolchain.sh). A biblioteca LVGL (src/lvgl) é compilada
# uma vez em build/lvgl-obj e reaproveitada.
OUT   ?= .
# UAPI = cabeçalhos do kernel (make headers_install), gerados pelo scripts/03-kernel.sh
UAPI  ?= $(HOME)/.cache/linux-no-iphone-6/uapi/include
LVGL  := ../../src/lvgl
OBJ   := ../../build/lvgl-obj
CC    := ../../host/iphone-cc
CFLAGS := -O2 -ffunction-sections -fdata-sections -Wall -Wno-unused-parameter \
          -DLV_CONF_INCLUDE_SIMPLE -I. -I../../src -idirafter $(UAPI)

FONTES := $(shell find $(LVGL)/src -name '*.c')
OBJS   := $(patsubst $(LVGL)/%.c,$(OBJ)/%.o,$(FONTES))

$(OUT)/inicio: inicio.c lv_conf.h $(OBJS) $(LVGL)/scripts/generators/built_in_font/Montserrat-Medium.ttf
	$(CC) $(CFLAGS) inicio.c $(OBJS) -Wl,--gc-sections -o $@
	llvm-strip $@

$(OBJ)/%.o: $(LVGL)/%.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
