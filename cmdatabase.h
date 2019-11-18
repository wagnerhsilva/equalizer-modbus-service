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
	uint16_t tensao;
	uint16_t temperatura;
	uint16_t impedancia;
	uint16_t timeout;
} BatteryAlarms_t;

typedef struct {
	uint16_t barramento;
	uint16_t target;
	uint16_t disco;
	BatteryAlarms_t bat_alarms[10240];
} Database_SharedMem_t;

int	CMDB_new(const char *SourcePath);
int	CMDB_get_alarmData(CMState * State, modbus_mapping_t *Mapping, Database_SharedMem_t *sharedMem);
int	CMDB_get_stringData(CMState * State, modbus_mapping_t *Mapping);
int	CMDB_get_batteryInfo(CMState *State);
#endif
