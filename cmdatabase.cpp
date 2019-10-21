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

int CMDB_get_stringData(CMState * State, modbus_mapping_t *Mapping, Database_SharedMem_t *sharedMem) {
    int ret = 0;
    int i = 0;
    int nbElements = State->BatteryCount*State->StringCount;
    int Address = 0;
    int nbColumns = 0;
    char SQLQuery[512] = { 0 };
    struct sqlite3_stmt *Statement;

	// printf("CMDB_getStringData:Inicio\n");

	/*
	 * Incluindo o conteudo da memoria compartilhada. A partir da posicao 0
	 * Os primeiros registradores sao os alarmes principais
	 */
	for(i=0;i<6;i++) {
		Mapping->tab_registers[Address++] = sharedMem->alarms[i];
	}
	/*
	 * Endereco inicial dos status de timeout e feito a partir do endereco
	 * 10200.
	 */
	Address = 10200;
	for(i=0;i<10240;i++) {
		Mapping->tab_registers[Address++] = sharedMem->read_state[i];
	}

	/*
	 * As proximas informacoes serao armazenadas a partir do endereco 11000,
	 * e nao mais 20000 como era antes.
	 */
	Address = 11000;

    /*
     * Construindo a consulta SQL
     */
    sprintf(SQLQuery,"SELECT temperatura, impedancia, tensao, equalizacao, batstatus FROM DataLogRT LIMIT %d;",nbElements);
//    printf("String SQL:%s\n",SQLQuery);

    /*
     * Inicializando a tabela com os valores ja conhecidos
     */
    Mapping->tab_registers[Address++] = (uint16_t) State->BatteryCount;
//    printf("tab_register[%d]=%d\n",(Address-1),Mapping->tab_registers[Address-1]);
    Mapping->tab_registers[Address++] = (uint16_t) State->StringCount;
//    printf("tab_register[%d]=%d\n",(Address-1),Mapping->tab_registers[Address-1]);

    /*
     * Executando a consulta
     */
//    printf("Executando busca ...\n");
    if(sqlite3_prepare_v2(SQLDatabase, SQLQuery,-1, &Statement, NULL) != SQLITE_OK){
    	printf("Failed to fetch data: %s\n", sqlite3_errmsg(SQLDatabase));
    	return -1;
    }

    /*
     * Recebendo os dados
     */
    while(sqlite3_step(Statement) != SQLITE_DONE) {
    	nbColumns = sqlite3_column_count(Statement);
//    	printf("Numero de colunas: %d\n",nbColumns);
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
    		Mapping->tab_registers[Address++] = (int)sqlite3_column_double(Statement, i);
//    		printf("tab_register[%d]=%d\n",(Address-1),Mapping->tab_registers[Address-1]);
    	}
    }

    /*
     * Encerrando os trabalhos
     */
    sqlite3_finalize(Statement);

//    printf("CMDB_getStringData:Final\n");

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
