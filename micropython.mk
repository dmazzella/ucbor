MOD_UCBOR_DIR := $(USERMOD_DIR)

CFLAGS_USERMOD += -DMICROPY_PY_UCBOR
CFLAGS_USERMOD += -DMICROPY_PY_UCBOR_CANONICAL
CFLAGS_USERMOD += -I$(MOD_UCBOR_DIR)
CFLAGS_USERMOD += -I$(MOD_UCBOR_DIR)/tinycbor/src/

SRC_USERMOD += $(MOD_UCBOR_DIR)/tinycbor/src/cborencoder.c
SRC_USERMOD += $(MOD_UCBOR_DIR)/tinycbor/src/cborerrorstrings.c
SRC_USERMOD += $(MOD_UCBOR_DIR)/tinycbor/src/cborparser.c
SRC_USERMOD += $(MOD_UCBOR_DIR)/tinycbor/src/cborparser_dup_string.c

SRC_USERMOD += $(MOD_UCBOR_DIR)/modcbor.c
