#include <modbus.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sqlite3.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>

#define DATABASE_PATH           "/var/www/equalizer-api/equalizer-api/equalizerdb"
#define SHARED_MEM_NAME         "/posix-shared-mem"
#define MODBUS_TCP_DEFAULT_PORT 502

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

int busca_strings(modbus_mapping_t *Mapping, int num_baterias, int num_strings) {
    int ret = 0;
    int i = 0;
    int nbElements = num_baterias * num_strings;
    int Address = 0;
    int nbColumns = 0;
	int nbIteracoes = 0;
    char SQLQuery[512] = { 0 };
    struct sqlite3_stmt *Statement;
	sqlite3 *Sqlite;

	if (sqlite3_open(DATABASE_PATH, &Sqlite) != SQLITE_OK) {
        return -1;
    }

    /*
     * Construindo a consulta SQL
     */
    sprintf(SQLQuery,"SELECT temperatura, impedancia, tensao, equalizacao, batstatus FROM DataLogRT LIMIT %d;",nbElements);

    /*
     * Executando a consulta
     */
	printf("Preparando busca\n");
    if(sqlite3_prepare_v2(Sqlite, SQLQuery,strlen(SQLQuery), &Statement, NULL) != SQLITE_OK){
    	printf("Failed to fetch data: %s\n", sqlite3_errmsg(Sqlite));
		sqlite3_close(Sqlite);
    	return -1;
    }

    /*
     * Recebendo os dados
     */
	printf("Varrendo dados\n");
    while(sqlite3_step(Statement) != SQLITE_DONE) {
		nbIteracoes++;
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
    		Mapping->tab_registers[Address++] = (int)sqlite3_column_double(Statement, i);
    	}
    }

    /*
     * Encerrando os trabalhos
     */
	printf("Finalizando busca\n");
    sqlite3_finalize(Statement);
	sqlite3_close(Sqlite);

    return ret;
}

int busca_alarmes(modbus_mapping_t *Mapping, Database_SharedMem_t *sharedMem, int num_baterias, int num_strings) {
    int Address = 0;
	int i = 0;
	int nbRegisters = num_strings * num_strings;

	/* Primeiros registros sao os fixos */
	Mapping->tab_registers[Address++] = (uint16_t) num_baterias;
    Mapping->tab_registers[Address++] = (uint16_t) num_strings;
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

int busca_info_baterias(int *num_baterias, int *num_strings, int *num_campos) {
	int ret = 0;
	int nbColumns = 0;
    int _num_baterias = 0;
    int _num_strings = 0;
	struct sqlite3_stmt *Statement;
	char SQLQuery[] = "SELECT n_baterias_por_strings, n_strings FROM Modulo LIMIT 1";
    sqlite3 *Sqlite;

	if (sqlite3_open(DATABASE_PATH, &Sqlite) != SQLITE_OK) {
        return -1;
    }

	printf("Executando busca (baterias)...\n");
	if(sqlite3_prepare_v2(Sqlite, SQLQuery, strlen(SQLQuery), &Statement, NULL) != SQLITE_OK){
		printf("Failed to fetch data: %s\n", sqlite3_errmsg(Sqlite));
        sqlite3_close(Sqlite);
		return -1;
	}
	printf("Processando resultado ...\n");
	/* Buscando os resultados desejados */
	while(sqlite3_step(Statement) != SQLITE_DONE) {
		nbColumns = sqlite3_column_count(Statement);
		printf("Numero de colunas: %d\n",nbColumns);
		if (nbColumns != 2) {
			printf("Erro na leitura das colunas: %d\n",nbColumns);
			ret = -1;
			break;
		}
		_num_baterias = sqlite3_column_int(Statement, 0);
		_num_strings = sqlite3_column_int(Statement, 1);
	}

    if (ret == -1) {
        sqlite3_finalize(Statement);
        sqlite3_close(Sqlite);
        return ret;
    }

	/* Sanity Check */
	if ((_num_baterias == 0) || (_num_strings == 0)) {
		printf("ERRO: Valores nulos para BatteryCount ou StringCount\n");
		ret = -1;
	} 

    *num_baterias = _num_baterias;
    *num_strings = _num_strings;
	/* Complementando com o numero fixo de campos que deve constar na tabela
	 * modbus, por leitura de bateria.
	 */
	*num_campos = 5;

	/*
	 * Encerrando os trabalhos
	 */
	sqlite3_finalize(Statement);
    sqlite3_close(Sqlite);

	return ret;
}

int main(void) {
    int ret = 0;
    int dbFileExists = 0;
    int fd_shm;
    int num_baterias = 0;
    int num_strings = 0;
    int num_campos = 0;
    int element_count = 0;
    int mb_socket = 0;
    int header_length = 0;
    int requested_address = 0;
    modbus_t *modbus = NULL;
    modbus_mapping_t *map_alarms;
    modbus_mapping_t *map_battinfo;
    modbus_mapping_t *mb_mapping;
    Database_SharedMem_t *shared_mem_ptr;
    uint8_t Query[MODBUS_TCP_MAX_ADU_LENGTH];

    /*
     * Checa se o arquivo com o banco de dados existe. Caso nao
     * existe, aguarda pela sua criacao
     */
    printf("Checa se arquivo de banco de dados existe\n");
    while(!dbFileExists) {
        if (access(DATABASE_PATH,F_OK) != -1) {
            dbFileExists = 1;
        } else {
            printf("Aguardando criacao de arquivo\n");
            sleep(1);
        }
    }
    
    /*
     * Inicializando a memoria compartilhada
     */
    printf("Inicializando a memoria compartilhada (alarmes)\n");
    if ((fd_shm = shm_open(SHARED_MEM_NAME, O_RDWR, 0)) == -1) {
		printf("Error shm_open\n");
        return 1;
    }

	if ((shared_mem_ptr = (Database_SharedMem_t*)mmap(NULL, sizeof(Database_SharedMem_t),
			PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)) == MAP_FAILED) {
		printf("Error mmap\n");
        return 1;
    }
    printf("Memoria compartilhada iniciada\n");

    /*
     * Recuperando a quantidade de strings e bateriais
     */
    if (busca_info_baterias(&num_baterias,&num_strings,&num_campos) != 0) {
        munmap(shared_mem_ptr, sizeof(Database_SharedMem_t));
        return 1;
    }
	printf("Numero de baterias = %d\n",num_baterias);
	printf("Numero de strings  = %d\n",num_strings);

    /*
     * Iniciando o Modbus
     */
    modbus = modbus_new_tcp("0", MODBUS_TCP_DEFAULT_PORT);
    if(!modbus){
    	printf("Failed to create Modbus TCP context\n");
        munmap(shared_mem_ptr, sizeof(Database_SharedMem_t));
        close(fd_shm);
        return -1; 
    }
    modbus_set_debug(modbus, TRUE);

    /*
     * Mapeamento de tabelas Modbus
     */
    element_count = 3 + num_baterias * num_strings * 4;
    printf("Alarm setup: ElementCount = %d\n",element_count);
    if (element_count >= 20000) {
        printf("ERRO: Quantidade de registros a serem criados e superior a 20000: %d\n", element_count);
        modbus_free(modbus);
        munmap(shared_mem_ptr, sizeof(Database_SharedMem_t));
        close(fd_shm);
        return -1;
    }
    /* Mapeando alarmes */
    map_alarms = modbus_mapping_new_start_address(0, 0, 0, 0, 0, element_count, 0, 0);
    if (map_alarms == NULL) {
    	printf("Failed to allocate the mapping for alarm data: %d\n",modbus_strerror(errno));
        modbus_free(modbus);
        munmap(shared_mem_ptr, sizeof(Database_SharedMem_t));
        close(fd_shm);
        return -1;
    }

    element_count = num_baterias * num_strings * num_campos + 2;
    printf("Info setup: ElementCount = %d\n",element_count);
    if (element_count >= 30000) {
        printf("ERRO: Quantidade de registros a serem criados e superior a 20000: %d\n", element_count);
        modbus_mapping_free(map_alarms);
        modbus_free(modbus);
        munmap(shared_mem_ptr, sizeof(Database_SharedMem_t));
        close(fd_shm);
        return -1;
    }
    /* Mapeando tabela de informacoes de bateria */
    map_battinfo = modbus_mapping_new_start_address(0, 0, 0, 0, 20000, element_count, 0, 0);
    if (map_battinfo == NULL) {
    	printf("Failed to allocate the mapping for info data: %d\n",modbus_strerror(errno));
        modbus_mapping_free(map_alarms);
        modbus_free(modbus);
        munmap(shared_mem_ptr, sizeof(Database_SharedMem_t));
        close(fd_shm);
        return -1;
    }

    printf("modbus_listen\n");
    mb_socket = modbus_tcp_listen(modbus, 1);
    if(mb_socket == -1){
        printf("Failed to listen : %d\n",modbus_strerror(errno));
        modbus_mapping_free(map_alarms);
        modbus_mapping_free(map_battinfo);
        modbus_free(modbus);
        munmap(shared_mem_ptr, sizeof(Database_SharedMem_t));
        close(fd_shm);
        return -1;
    }
    
    header_length = modbus_get_header_length(modbus);

    while(1) {
        /*
         * Aguarda uma nova conexao
         */
        modbus_tcp_accept(modbus, &mb_socket);
        /* 
         * Funciona enquanto estiver conectado
         */
        while(1) {
            /* RECEIVE */
            do {
                ret = modbus_receive(modbus, Query);
            } while(ret == 0);
            if (ret == -1) {
                printf("Erro modbus_receive()\n");
                break;
            }

            /* Checa se e o comando correto */
            if (Query[header_length] == 0x03) {
                /* Read Registers - o caso configurado */
                requested_address = MODBUS_GET_INT16_FROM_INT8(Query,header_length+1);
                /*
                 * Atualiza informações da tabela para preenchimento da resposta.
                 * Mapeamento padrao e o de alarmes
                 */
                mb_mapping = map_alarms;

                /* Checa se esta na faixa registrada */
                if ((requested_address > 0) && 
                        (requested_address < map_alarms->start_registers)) {
                    if (busca_alarmes(map_alarms,shared_mem_ptr,num_baterias,num_strings) == -1) {
                        break;
                    }
                } else if ((requested_address >= 20000) && (requested_address < (20000+map_battinfo->start_registers))) {
                    printf("Buscando os dados\n");
                    if (busca_strings(map_battinfo,num_baterias,num_strings) == -1) {
                        break;
                    }
                    printf("Atualizando mapa\n");
                    /* Chaveia o mapeamento para as informacoes da bateria */
                    mb_mapping = map_battinfo;
                }
            }

            /* REPLY */
            printf("Montando resposta\n");
            ret = modbus_reply(modbus, Query, ret, mb_mapping);
            if (ret == -1) {
                printf("Erro modbus_reply()\n");
                break;
            }
            printf("Resposta enviada\n");
        }
    }

    /*
     * Encerrando modbus
     */
    modbus_mapping_free(map_alarms);
    modbus_mapping_free(map_battinfo);
    modbus_free(modbus);

    /*
     * Finalizando a memoria compartilhada
     */
    if (munmap(shared_mem_ptr, sizeof(Database_SharedMem_t)) == -1) {
        printf("Error munmap\n");
        return 1;
    }

    close(fd_shm);

    return 0;
}