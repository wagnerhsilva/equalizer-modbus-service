#if !defined(CM_DATABASE_H)
#define CM_DATABASE_H

#include <stdint.h>

struct CMDatabase;

struct CMState{
    int StringCount;
    int BatteryCount;
    int FieldCount;
};

typedef struct {
	uint16_t alarms[6];
	uint16_t read_state[10240];
} Database_SharedMem_t;

int	CMDB_new(const char *SourcePath);
int	CMDB_get_stringData(CMState * State, modbus_mapping_t *Mapping, Database_SharedMem_t *sharedMem);
int	CMDB_get_batteryInfo(CMState *State);
#endif
