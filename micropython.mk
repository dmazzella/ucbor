MOD_UCBOR_DIR := $(USERMOD_DIR)

CFLAGS_USERMOD += -DMICROPY_PY_UCBOR
CFLAGS_USERMOD += -DMICROPY_PY_UCBOR_CANONICAL
CFLAGS_USERMOD += -I$(MOD_UCBOR_DIR)

SRC_USERMOD += $(MOD_UCBOR_DIR)/modcbor.c
