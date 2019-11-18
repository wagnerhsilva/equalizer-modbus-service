#include <stdint.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modbus.h>
#include "cmdatabase.h"

#define FIELDS_PASSED 5 //temperatura, impedancia, tensao, equalizacao, batstatus

#define DATABASE_SOURCE_DATA    "DataLogRT"
#define DATABASE_MODULO_DATA    "Modulo"

static sqlite3 *SQLDatabase;

int CMDB_get_alarmData(CMState * State, modbus_mapping_t *Mapping, Database_SharedMem_t *sharedMem) {
	int Address = 0;
	int i = 0;
	int nbRegisters = State->BatteryCount * State->StringCount;

	/* Primeiros registros sao os fixos */
	Mapping->tab_registers[Address++] = (uint16_t) State->BatteryCount;
    Mapping->tab_registers[Address++] = (uint16_t) State->StringCount;
	Mapping->tab_registers[Address++] = sharedMem->barramento;
	Mapping->tab_registers[Address++] = sharedMem->target;
	Mapping->tab_registers[Address++] = sharedMem->disco;

	/* Alarmes relacionados a cada bateria */
	for(i=0;i<nbRegisters;i++) {
		Mapping->tab_registers[Address++] = sharedMem->bat_alarms[i].tensao;
		Mapping->tab_registers[Address++] = sharedMem->bat_alarms[i].temperatura;
		Mapping->tab_registers[Address++] = sharedMem->bat_alarms[i].impedancia;
		Mapping->tab_registers[Address++] = sharedMem->bat_alarms[i].timeout;
		/* Pula os enderecos esperados para as leituras */
		Address += 5;
	}

	return 0;
}

int CMDB_get_stringData(CMState * State, modbus_mapping_t *Mapping) {
    int ret = 0;
    int i = 0;
    int nbElements = State->BatteryCount * State->StringCount;
    int Address = 9; /* Considerando offset dos 5 primeiros registros + 4 primeiros alarmes */
    int nbColumns = 0;
    char SQLQuery[512] = { 0 };
    struct sqlite3_stmt *Statement;

    /*
     * Construindo a consulta SQL
     */
    sprintf(SQLQuery,"SELECT temperatura, impedancia, tensao, equalizacao, batstatus FROM DataLogRT LIMIT %d;",nbElements);

    /*
     * Executando a consulta
     */
    if(sqlite3_prepare_v2(SQLDatabase, SQLQuery,-1, &Statement, NULL) != SQLITE_OK){
    	printf("Failed to fetch data: %s\n", sqlite3_errmsg(SQLDatabase));
    	return -1;
    }

    /*
     * Recebendo os dados
     */
    while(sqlite3_step(Statement) != SQLITE_DONE) {
    	nbColumns = sqlite3_column_count(Statement);
    	/* Sanity check */
    	if (nbColumns != 5) {
    		printf("Erro na recuperacao da linha do banco de dados: %d\n",nbColumns);
    		ret = -1;
    		break; /* Sai do loop */
    	}
    	/* A informação é armazenada em FLOAT no banco, mesmo
    	 * não havendo tratamento previo para isso.
    	 */
    	for (i=0;i<nbColumns;i++) {
			// printf("Preenchendo posicao %d\n",Address);
    		Mapping->tab_registers[Address++] = (int)sqlite3_column_double(Statement, i);
    	}
		/* Para o proximo registro, considerar o offset de 4 posicoes de alarmes */
		Address += 4;
    }

    /*
     * Encerrando os trabalhos
     */
    sqlite3_finalize(Statement);

    return ret;
}

int CMDB_get_batteryInfo(CMState *State) {
	int ret = 0;
	int nbColumns = 0;
	int i = 0;
	struct sqlite3_stmt *Statement;
	char SQLQuery[] = "SELECT n_baterias_por_strings, n_strings FROM Modulo LIMIT 1";

//	printf("CMDB_get_batteryInfo:Inicio\n");

	/* Realizando busca */
//	printf("Executando busca ...\n");
	if(sqlite3_prepare_v2(SQLDatabase, SQLQuery,-1, &Statement, NULL) != SQLITE_OK){
		printf("Failed to fetch data: %s\n", sqlite3_errmsg(SQLDatabase));
		return -1;
	}

	/* Buscando os resultados desejados */
	while(sqlite3_step(Statement) != SQLITE_DONE) {
		nbColumns = sqlite3_column_count(Statement);
//		printf("Numero de colunas: %d\n",nbColumns);
		if (nbColumns != 2) {
			printf("Erro na leitura das colunas: %d\n",nbColumns);
			ret = -1;
			break;
		}
		State->BatteryCount = sqlite3_column_int(Statement, 0);
		State->StringCount = sqlite3_column_int(Statement, 1);
	}

	/* Resultado final */
//	printf("BatteryCount = %d\n",State->BatteryCount);
//	printf("StringCount  = %d\n",State->StringCount);
	/* Sanity Check */
	if ((State->BatteryCount == 0) || (State->StringCount == 0)) {
		printf("ERRO: Valores nulos para BatteryCount ou StringCount\n");
		ret = -1;
	}

	/* Complementando com o numero fixo de campos que deve constar na tabela
	 * modbus, por leitura de bateria.
	 */
	State->FieldCount = FIELDS_PASSED;

	/*
	 * Encerrando os trabalhos
	 */
	sqlite3_finalize(Statement);

//	printf("CMDB_get_batteryInfo:Final\n");

	return ret;
}


int CMDB_new(const char *SourcePath){
	int ret = 0;

    int Err = sqlite3_open(SourcePath, &SQLDatabase);
    if(Err != 0){
        ret = -1;
    }
    
    return ret;
}
