#if !defined(CM_DATABASE_H)
#define CM_DATABASE_H

#include <stdint.h>

struct CMDatabase;

struct CMState{
    int StringCount;
    int BatteryCount;
    int FieldCount;
    float * LinearDataLogRT;
};

CMDatabase *    CMDB_new(const char *SourcePath);
bool            CMDB_query_state(CMDatabase * Database, CMState *State);
bool            CMDB_fetch_data(CMDatabase * Database, CMState *State);
#endif
