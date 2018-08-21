#if !defined(CM_DATABASE_H)
#define CM_DATABASE_H

#include <stdint.h>

struct CMDatabase;

struct CMState{
    int StringCount;
    int BatteryCount;
    int FieldCount;
};

int	CMDB_new(const char *SourcePath);
int	CMDB_get_stringData(CMState * State, modbus_mapping_t *Mapping);
int	CMDB_get_batteryInfo(CMState *State);
#endif
