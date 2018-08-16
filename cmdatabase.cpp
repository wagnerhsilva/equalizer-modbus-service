#include <stdint.h>
#include <sqlite3.h>
#include <string>
#include <iostream>
#include <map>
#include <sstream>
#include <string.h>

#define FIELDS_PASSED 5 //temperatura, impedancia, tensao, equalizacao, batstatus

#define DATABASE_SOURCE_DATA    "DataLogRT"
#define DATABASE_PARAMETER_DATA "Parameters"
#define DATABASE_MODULO_DATA    "Modulo"

#define MAKE_STR(y, x) __to_str<x>(y)

template<typename T>
static std::string __to_str(T Value){
    std::ostringstream ss;
    ss << Value;
    std::string Output(ss.str());
    return Output;
}

struct CMDatabase{
    sqlite3 *SQLDatabase;
    int OnQuery;
    int QueryIndex;
    int QueryMaxItens;
    struct sqlite3_stmt *Statement;

    std::map<std::string, int> ConfigurationMap;
};

struct QueryRet{
    std::string Text;
    std::string Column;
    bool HasData;
};

struct CMState{
    int StringCount;
    int BatteryCount;
    int FieldCount;
    /* Flavio Alves: mudança para int */
    int * LinearDataLogRT;
};

static QueryRet CMDB_looped_query_begin(CMDatabase * Database, std::string SQLQuery){
    Database->OnQuery = false;
    int Result = sqlite3_prepare_v2(Database->SQLDatabase, SQLQuery.c_str(),
                                     -1, &Database->Statement, NULL);
    QueryRet Return;
    Return.HasData = false;
    if(Result == SQLITE_OK){
        if(sqlite3_step(Database->Statement) == SQLITE_ROW){
            int Columns = sqlite3_column_count(Database->Statement);
            Database->QueryIndex = 0;
            Database->QueryMaxItens = Columns;
            const void *VData = sqlite3_column_text(Database->Statement,
                                                    Database->QueryIndex);
            if(VData){
                Return.Text = std::string((const char*)VData);
            }else{
                Return.Text = std::string("");
            }
            Return.Column = std::string((const char *)sqlite3_column_name(Database->Statement,
                                                                          Database->QueryIndex));
            Database->OnQuery = true;
            Return.HasData = true;
        }
    }
    
    return Return;
}

static bool CMDB_looped_query_continue(CMDatabase * Database){
    Database->QueryIndex += 1;
    bool ShouldContinue = (Database->OnQuery && Database->QueryIndex < Database->QueryMaxItens);
    if(!ShouldContinue){
        ShouldContinue = (sqlite3_step(Database->Statement) == SQLITE_ROW);
        if(ShouldContinue){
            Database->QueryIndex = 0;
            Database->QueryMaxItens = sqlite3_column_count(Database->Statement);
        }
    }
    Database->OnQuery = ShouldContinue;
    return ShouldContinue;
}

static QueryRet CMDB_looped_query_next(CMDatabase * Database){
    QueryRet Return;
    Return.HasData = false;
    if(Database->QueryIndex < Database->QueryMaxItens){
        const void * VData = sqlite3_column_text(Database->Statement,
                                                 Database->QueryIndex);
        if(VData){
            Return.Text = std::string((const char*)VData);
        }else{
            Return.Text = std::string("");
        }
        Return.Column = std::string((const char *)sqlite3_column_name(Database->Statement,
                                                                      Database->QueryIndex));
        Return.HasData = true;
    }
    return Return;
}

bool CMDB_fetch_data(CMDatabase * Database, CMState * State){
//	printf("CMDB_fetch_data\n");
    bool Result = true;
    /* Flavio Alves: simplificação da consulta */
    //std::string SQLQuery("SELECT temperatura, impedancia, tensao, equalizacao, batstatus FROM DataLogRT");

    std::string SQLQuery("SELECT temperatura, impedancia, tensao, equalizacao, batstatus FROM DataLogRT as RVAL");
    SQLQuery += " WHERE CAST(SUBSTR(RVAL.string, 2, length(RVAL.string)) as integer) <= " + MAKE_STR(State->StringCount, int);
    SQLQuery += " AND CAST(SUBSTR(RVAL.bateria, 2, length(RVAL.bateria)) as integer) <= " + MAKE_STR(State->BatteryCount, int);
    SQLQuery += " ORDER BY CAST(SUBSTR(RVAL.string, 2, length(RVAL.string)) as integer),";
    SQLQuery += " CAST(SUBSTR(RVAL.bateria, 2, length(RVAL.bateria)) as integer);";
    

    int Index = 0;
    QueryRet iterator = CMDB_looped_query_begin(Database, SQLQuery);
    /* Flavio Alves: mudança para int */
    State->LinearDataLogRT[Index++] = (int) atoi(iterator.Text.c_str());
    Result &= iterator.HasData;
    
    while(CMDB_looped_query_continue(Database)){
        iterator = CMDB_looped_query_next(Database);
        /* Flavio Alves: mudança para int */
        State->LinearDataLogRT[Index++] = (int) atoi(iterator.Text.c_str());
        Result &= iterator.HasData;
    }
  
    return Result;
}

static bool CMDB_set_configuration(CMDatabase * Database, CMState *State){
    bool Updated = false;
    std::string SQLQuery("SELECT * FROM " DATABASE_MODULO_DATA " LIMIT 1;");
    QueryRet iterator;
    std::map<std::string, int>::iterator MapIterator;
    
    iterator = CMDB_looped_query_begin(Database, SQLQuery);
    
    MapIterator = Database->ConfigurationMap.find(iterator.Column);
        
    if(MapIterator != Database->ConfigurationMap.end()){
        MapIterator->second = atoi(iterator.Text.c_str()); 
    }

    while(CMDB_looped_query_continue(Database)){
        iterator = CMDB_looped_query_next(Database);
        MapIterator = Database->ConfigurationMap.find(iterator.Column);
        
        if(MapIterator != Database->ConfigurationMap.end()){
            MapIterator->second = atoi(iterator.Text.c_str()); 
        }
    }
    
    int BatteryCount = Database->ConfigurationMap["n_baterias_por_strings"];
    int StringCount  = Database->ConfigurationMap["n_strings"]; 
        

    if(State->BatteryCount != BatteryCount || StringCount != State->StringCount){
        State->BatteryCount = BatteryCount;
        State->StringCount  = StringCount;
        if(State->LinearDataLogRT){
            delete State->LinearDataLogRT;
        }
        
        int TotalElementCount = State->StringCount * State->BatteryCount * FIELDS_PASSED;
        int MemoryBlock = TotalElementCount; //each field takes 2 uint16_t (floats)
        /* Flavio Alves: mudança de float para int */
        State->LinearDataLogRT = new int[MemoryBlock];
        State->FieldCount = FIELDS_PASSED;
        memset(State->LinearDataLogRT, 0, sizeof(int) * MemoryBlock);
        Updated = CMDB_fetch_data(Database, State);
    }
    
    return Updated;
}


CMDatabase * CMDB_new(const char *SourcePath){
    CMDatabase *Database = new CMDatabase;
    int Err = sqlite3_open(SourcePath, &Database->SQLDatabase);
    if(Err != 0){
        delete Database;
        return nullptr; 
    }
    
    Database->ConfigurationMap["n_strings"] = -1;
    Database->ConfigurationMap["n_baterias_por_strings"] = -1;
    return Database;
}

bool CMDB_query_state(CMDatabase *Database, CMState *State){
    return CMDB_set_configuration(Database, State);
}
