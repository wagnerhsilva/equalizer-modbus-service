#include <modbus.h>
#include <errno.h>
#include <cmdatabase.h>
#include <timer.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define MODBUS_START_ADDRESS    0
#define MAX_CONNECTIONS         1
#define FETCH_TIMEOUT           10 * 1000 // 1min
#define DATABASE_PATH           "/var/www/equalizer-api/equalizer-api/equalizerdb"

/*
 * Estrutura da memoria compartilhada
 */
#define SHARED_MEM_NAME "/posix-shared-mem"

Database_SharedMem_t *shared_mem_ptr;
int fd_shm;

/*
 * Simple module that reads from SQL database (sqlite3), translates the data
 * into a double uint16_t values and sets a modbus tcp client for querying
*/


/*
 * Our definition of Server: a modbus context, a socket and some mapping
 * information
*/
typedef struct{
    modbus_t *Context; //tcp context
    uint8_t Query[MODBUS_TCP_MAX_ADU_LENGTH]; //query arrived
    int HeaderLength; 
    int ClientSocket; //socket currently being used
    modbus_mapping_t *Mapping; //main mapping structure
}Server_t;

static CMState State;

/*
 * Handles a connection. Whenever our socket receives a client 
 * we handled the connection here untill it disconnects
*/
int server_handle_connection(Server_t *Server, CMState *State){
    int rc = 0;
    bool finished = false;
    int ElementCount = 0;
    int currentBatteryCount = State->BatteryCount;
    int currentStringCount = State->StringCount;
    int header_lenght = modbus_get_header_length(Server->Context);
    int requested_address = 0;

    /* Roda indefinidamente */
    while(!finished){
    	/* RECEIVE */
        do {
            rc = modbus_receive(Server->Context, Server->Query);
        } while(rc == 0);
        if (rc == -1) {
        	printf("Erro modbus_receive()\n");
            break;
        }

        /* Checa se e o comando correto */
        if (Server->Query[header_lenght] == 0x03) {
            /* Read Registers - o caso configurado */
            requested_address = MODBUS_GET_INT16_FROM_INT8(Server->Query,header_lenght+1);
            /*
             * Atualiza informações da tabela para preenchimento da resposta
             */
            if (CMDB_get_alarmData(State,Server->Mapping,shared_mem_ptr) == -1) {
                // printf("Erro atualização tabela modbus\n");
                modbus_mapping_free(Server->Mapping);
                modbus_free(Server->Context);
                break;
            }
            if (CMDB_get_stringData(State,Server->Mapping) == -1) {
                // printf("Erro atualização tabela modbus\n");
                modbus_mapping_free(Server->Mapping);
                modbus_free(Server->Context);
                break;
            }
        }

        /* REPLY */
        rc = modbus_reply(Server->Context, Server->Query, rc, mb_mapping);
        if (rc == -1) {
            printf("Erro modbus_reply()\n");
            break;
        }

        /* 
         * ATUALIZA TABELA 
         */
        CMDB_get_batteryInfo(State);
        if ((State->BatteryCount != currentBatteryCount) || (State->StringCount != currentStringCount)) {
        	printf("Reset registers ...\n");
        	/* Limpa as tabelas atuais */
            modbus_mapping_free(Server->AlarmMapping);
        	modbus_mapping_free(Server->InfoMapping);
            /* Mapeia a tabela de alarmes */
            ElementCount = 3 + State->BatteryCount * State->StringCount * 4;
            printf("Alarm setup: ElementCount = %d\n",ElementCount);
            if (ElementCount >= 20000) {
                printf("ERRO: Quantidade de registros a serem criados e superior a 20000: %d\n", ElementCount);
                return -1;
            }
            Server->AlarmMapping = modbus_mapping_new_start_address(0, 0, 0, 0, MODBUS_START_ADDRESS_ALARMS, ElementCount, 0, 0);
            if (Server->AlarmMapping == NULL) {
                printf("Failed to allocate the mapping for alarm data: %d\n",modbus_strerror(errno));
                return -1;
            }
            /* Mapeia tabela de informacoes */
            ElementCount = State->BatteryCount * State->StringCount * State->FieldCount;
        	/* Cria uma nova tabela, com o tamanho atualizado */
            printf("Info setup: ElementCount = %d\n",ElementCount);
            if (ElementCount >= 30000) {
                printf("ERRO: Quantidade de registros a serem criados e superior a 30000: %d\n", ElementCount);
                modbus_mapping_free(Server->AlarmMapping);
                return -1;
            }
        	Server->InfoMapping = modbus_mapping_new_start_address(0, 0, 0, 0, MODBUS_START_ADDRESS_INFO, ElementCount + 2, 0, 0);
        	if (Server->InfoMapping == NULL) {
        		printf("Failed to allocate the mapping\n");
                modbus_mapping_free(Server->AlarmMapping);
        		return -1;
        	}
        }

        /*
         * Atualiza contador
         */
        currentBatteryCount = State->BatteryCount;
        currentStringCount  = State->StringCount;
    }

    return 0;
}

/*
 * Inits the modbus module and the timer object
*/
int start_modbus(CMState *State){
    Server_t Server;
    int ElementCount = 0;

    Server.Context = modbus_new_tcp(nullptr, MODBUS_TCP_DEFAULT_PORT);
    if(!Server.Context){
    	printf("Failed to create Modbus TCP context\n");
        return -1; 
    } 
    
    // Configura o debug da libmodbus
   modbus_set_debug(Server.Context, TRUE);

    /*
     * Inicializa a tabela de registradores
     * Dados de alarmes
     */
    ElementCount = 3 + State->BatteryCount * State->StringCount * 4;
    printf("Alarm setup: ElementCount = %d\n",ElementCount);
    if (ElementCount >= 20000) {
        printf("ERRO: Quantidade de registros a serem criados e superior a 20000: %d\n", ElementCount);
        modbus_free(Server.Context);
        return -1;
    }
    
    Server.AlarmMapping = modbus_mapping_new_start_address(0, 0, 0, 0, MODBUS_START_ADDRESS_ALARMS, ElementCount, 0, 0);
    if (Server.AlarmMapping == NULL) {
    	printf("Failed to allocate the mapping for alarm data: %d\n",modbus_strerror(errno));
        modbus_free(Server.Context);
        return -1;
    }

    /* Preenche a memória */
    if (CMDB_get_alarmData(State, Server.AlarmMapping, shared_mem_ptr) == -1) {
    	printf("Failed go fill modbus table with database information\n");;
    	modbus_mapping_free(Server.AlarmMapping);
    	modbus_free(Server.Context);
    	return -1;
    }

    /*
     * Inicializa a tabela de registradores
     * Dados gerais do equipamento
     */
    ElementCount = State->BatteryCount * State->StringCount * State->FieldCount + 2;
    printf("Info setup: ElementCount = %d\n",ElementCount);
    if (ElementCount >= 30000) {
        printf("ERRO: Quantidade de registros a serem criados e superior a 30000: %d\n", ElementCount);
        modbus_mapping_free(Server.AlarmMapping);
        modbus_free(Server.Context);
        return -1;
    }

    printf("Mapeando ...\n");
    Server.InfoMapping = modbus_mapping_new_start_address(0, 0, 0, 0, MODBUS_START_ADDRESS_INFO, ElementCount, 0, 0);
    if (Server.InfoMapping == NULL) {
    	printf("Failed to allocate the mapping for info data: %d\n",modbus_strerror(errno));
        modbus_mapping_free(Server.AlarmMapping);
        modbus_free(Server.Context);
        return -1;
    }
    printf("Mapeado ...\n");

    /* Preenche a memória */
    if (CMDB_get_stringData(State, Server.InfoMapping) == -1) {
    	printf("Failed go fill modbus table with database information\n");
        modbus_mapping_free(Server.AlarmMapping);
    	modbus_mapping_free(Server.InfoMapping);
    	modbus_free(Server.Context);
    	return -1;
    }

    Server.ClientSocket = modbus_tcp_listen(Server.Context, MAX_CONNECTIONS);
    if(Server.ClientSocket == -1){
        printf("Failed to listen : %d\n",modbus_strerror(errno));
        return -1;
    }
    
    Server.HeaderLength = modbus_get_header_length(Server.Context);

    while(1){
        printf("Listening for connection\n");
        modbus_tcp_accept(Server.Context, &Server.ClientSocket);
        server_handle_connection(&Server, State);
        printf("Connection ended\n");
    }   
}

static bool exist_file(const char *name){
    return access(name, F_OK) != -1;
}

void WaitDatabase(const char *path){
    int exists = exist_file(path);
    int count = 0;
    while(!exists){
        printf("Waiting for Database creation [%d]\n",count++);
        sleep(1);
        exists = exist_file(path);
    }
}

/*
 * Entry point
*/
int main(int argc, char **argv){
	WaitDatabase(DATABASE_PATH);

	if (CMDB_new(DATABASE_PATH) == -1) {
		printf("Erro na inicialização do Banco de Dados\n");
		return 1;
	}

    // Get shared memory
	if ((fd_shm = shm_open(SHARED_MEM_NAME, O_RDWR, 0)) == -1) {
		printf("Error shm_open\n");
        return 1;
    }

	if ((shared_mem_ptr = (Database_SharedMem_t*)mmap(NULL, sizeof(Database_SharedMem_t),
			PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)) == MAP_FAILED) {
		printf("Error mmap\n");
        return 1;
    }

    printf("memoria compartilhada iniciada\n");

	/* Realiza uma busca no banco de dados e obtem a informação de quantidade de
	 * strings e de baterias por string do projeto.
	 */
	CMDB_get_batteryInfo(&State);
	printf("StringCount: %d|BatteryCount: %d\n",State.StringCount,State.BatteryCount);
	start_modbus(&State);

    if (munmap(shared_mem_ptr, sizeof(Database_SharedMem_t)) == -1) {
        printf("Error munmap\n");
        return 1;
    }
		

	return 0;
}
