CC = qcc
VFLAG = -Vgcc_ntox86_64
CFLAGS = $(VFLAG) -Wall -O2 -Iinclude
LDFLAGS = $(VFLAG) -lm -lsocket

BIN_DIR = bin

TARGETS = $(BIN_DIR)/surgical_arm_resmgr \
          $(BIN_DIR)/safety_supervisor \
          $(BIN_DIR)/ai_tremor_engine \
          $(BIN_DIR)/telemetry_bridge

all: $(BIN_DIR) $(TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/surgical_arm_resmgr: src/resmgr_arm/surgical_arm_resmgr.c include/neuroguard_ipc.h
	$(CC) $(CFLAGS) $< -o $@

$(BIN_DIR)/safety_supervisor: src/safety_supervisor/safety_supervisor.c include/neuroguard_ipc.h
	$(CC) $(CFLAGS) $< -o $@ -lm

$(BIN_DIR)/ai_tremor_engine: src/ai_tremor_engine/ai_tremor_engine.c include/neuroguard_ipc.h
	$(CC) $(CFLAGS) $< -o $@ -lm

$(BIN_DIR)/telemetry_bridge: src/telemetry_bridge/telemetry_bridge.c include/neuroguard_ipc.h
	$(CC) $(CFLAGS) $< -o $@ -lsocket

clean:
	rm -rf $(BIN_DIR) surgical_arm_resmgr