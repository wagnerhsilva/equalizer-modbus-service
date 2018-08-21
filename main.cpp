#include <modbus.h>
#include <errno.h>
#include <cmdatabase.h>
#include <timer.h>
#include <unistd.h>

#define MODBUS_START_ADDRESS 20000
#define MAX_CONNECTIONS 1
#define FETCH_TIMEOUT 10 * 1000 // 1min
#define DATABASE_PATH "/var/www/equalizer-api/equalizer-api/equalizerdb"

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

//    printf("server_handle_connection start\n");

    /* Roda indefinidamente */
    while(!finished){
//        printf("Receive ...\n");
    	/* RECEIVE */
        do{
            rc = modbus_receive(Server->Context, Server->Query);
        } while(rc == 0);
        if(rc == -1) {
        	printf("Erro modbus_receive()\n");
            break;
        }

        /*
         * Atualiza informações da tabela para preenchimento da resposta
         */
//        printf("Update table ...\n");
        if (CMDB_get_stringData(State,Server->Mapping) == -1) {
        	printf("Erro atualização tabela modbus\n");
        	modbus_mapping_free(Server->Mapping);
        	modbus_free(Server->Context);
        	break;
        }

//        printf("Reply ...\n");
        /* REPLY */
        rc = modbus_reply(Server->Context, Server->Query, rc, Server->Mapping);
        if (rc == -1) {
        	printf("Erro modbus_reply()\n");
        	break;
        }

//        printf("Check update ...\n");;
        /* ATUALIZA TABELA */
        CMDB_get_batteryInfo(State);
        if ((State->BatteryCount != currentBatteryCount) || (State->StringCount != currentStringCount)) {
        	printf("Reset registers ...\n");
        	ElementCount = State->BatteryCount * State->StringCount;
        	/* Limpa a tabela atual */
        	modbus_mapping_free(Server->Mapping);
        	/* Cria uma nova tabela, com o tamanho atualizado */
        	Server->Mapping = modbus_mapping_new_start_address(0, 0, 0, 0, MODBUS_START_ADDRESS, ElementCount + 2, 0, 0);
        	if (Server->Mapping == NULL) {
        		printf("Failed to allocate the mapping\n");
        		modbus_free(Server->Context);
        		break;
        	}
        }

        /*
         * Atualiza contador
         */
        currentBatteryCount = State->BatteryCount;
        currentStringCount  = State->StringCount;
    }

//    printf("server_handle_connection end");

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
//    modbus_set_debug(Server.Context, TRUE);

    /*
     * Inicializa a tabela de registradores
     */
    ElementCount = State->BatteryCount * State->StringCount * State->FieldCount;
    Server.Mapping = modbus_mapping_new_start_address(0, 0, 0, 0, MODBUS_START_ADDRESS, ElementCount + 2, 0, 0);
    if (Server.Mapping == NULL) {
    	printf("Failed to allocate the mapping: %d\n",modbus_strerror(errno));
        modbus_free(Server.Context);
        return -1;
    }

    /* Preenche a memória */
    if (CMDB_get_stringData(State, Server.Mapping) == -1) {
    	printf("Failed go fill modbus table with database information\n");;
    	modbus_mapping_free(Server.Mapping);
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

	/* Realiza uma busca no banco de dados e obtem a informação de quantidade de
	 * strings e de baterias por string do projeto.
	 */
	CMDB_get_batteryInfo(&State);
	printf("StringCount: %d|BatteryCount: %d\n",State.StringCount,State.BatteryCount);
	start_modbus(&State);

	return 0;
}
