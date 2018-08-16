#include <modbus.h>
#include <iostream>
#include <errno.h>
#include <sys/select.h>
#include <cmdatabase.h>
#include <timer.h>
#include <pthread.h>
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
 * Mutex controlls race conditions on the mapping structure for libmodbus
*/
static pthread_mutex_t MappingMutex;

/*
 * Our definition of Server: a modbus context, a socket and some mapping
 * information
*/
typedef struct{
    modbus_t *Context; //tcp context
    uint8_t *Query; //query arrived
    int HeaderLength; 
    int ClientSocket; //socket currently being used
    modbus_mapping_t *Mapping; //main mapping structure
}Server_t;

/*
 * This is basic pointer to a object that [can] start a thread 
 * so that we can time updates from the SQL database
*/
Later *LaterCall;


/*
 * Handles a connection. Whenever our socket receives a client 
 * we handled the connection here untill it disconnects
*/
int server_handle_connection(Server_t *Server){
    int rc = 0;
    bool finished = false;
    while(!finished){
        do{
            rc = modbus_receive(Server->Context, Server->Query);
            if(rc > 0){
                pthread_mutex_lock(&MappingMutex);
                modbus_reply(Server->Context, Server->Query, rc, Server->Mapping);
                pthread_mutex_unlock(&MappingMutex);
            }
        }while(rc == 0);
        
    
        if(rc == -1){
            std::cout << "Connection: " << modbus_strerror(errno) << std::endl;
            break;
        }
    }
    return 0;
}

/*
 * Clears the current Mapping being used by the Server and fill it 
 * with whatever is in the State, it should have come from the Database
 * handler structure
*/
void server_fill_mapping(Server_t * Server, CMState * State){
//	printf("Inicializando tabela MODBUS\n");

    if(Server->Mapping != 0){
        modbus_mapping_free(Server->Mapping);
    }
    int Address = 0;
    int ElementCount = State->BatteryCount * State->StringCount * State->FieldCount;
    int CountInUint16 = ElementCount  * 2; //everything is in floats, so double the uint16_t
    Server->Mapping = modbus_mapping_new_start_address(0, 0, 0, 0, MODBUS_START_ADDRESS, CountInUint16 + 2, 0, 0);
    
    Server->Mapping->tab_registers[Address++] = (uint16_t) State->BatteryCount;
    Server->Mapping->tab_registers[Address++] = (uint16_t) State->StringCount;
    
    for(int i = 0; i < ElementCount; i += 1){
        /* Flavio Alves: mudança de float para int 
         * float exigia 32 bits. O inteiro que vamos fornecer precisa de
         * apenas 16 bits. Então a regra de atualização muda também, sem
         * precisar mudar os registros de 2 em 2.
         */
        int TargetAddr = Address + i;
        Server->Mapping->tab_registers[TargetAddr] = (uint16_t)State->LinearDataLogRT[i];
        // modbus_set_float_abcd(State->LinearDataLogRT[i],
                    //    &(Server->Mapping->tab_registers[TargetAddr]));
    }

//    printf("Tabela inicializada\n");
}

/*
 * Callback invoked everytime the timer defined by LaterCall finishes
*/
void __timed_update(Server_t * Server, CMState * State, CMDatabase * Database){
    pthread_mutex_lock(&MappingMutex); //prevent any request during this time
    if(!CMDB_query_state(Database, State)){ //check if there were some configuration changes in Web
        /*
         * In case there were no updates we need to manually update the values from the Mapping
         * When the update accours the State is automaticly updated
        */
        CMDB_fetch_data(Database, State);
    }
    /*
     * Creates a new mapping from the fetched values
    */
    server_fill_mapping(Server, State);
    pthread_mutex_unlock(&MappingMutex); //allow requests to come now
    /*
     * Restart magic timer
    */
    delete LaterCall;
    LaterCall = new Later(FETCH_TIMEOUT, true, &__timed_update, Server, State, Database);
}

/*
 * Inits the modbus module and the timer object
*/
int init_modbus(CMDatabase *Database, CMState *State){
    Server_t *Server = new Server_t;
    Server->Context = modbus_new_tcp(nullptr, MODBUS_TCP_DEFAULT_PORT);
    if(!Server->Context){
        std::cout << "Failed to created Modbus TCP context" << std::endl;
        return -1; 
    } 
    
    if(pthread_mutex_init(&MappingMutex, NULL) != 0){
        std::cout << "Failed to initialize Mapping Mutex, good luck" << std::endl;
    }

    Server->Mapping = 0;

    server_fill_mapping(Server, State);

    Server->ClientSocket = modbus_tcp_listen(Server->Context, MAX_CONNECTIONS);
    Server->Query = new uint8_t[MODBUS_TCP_MAX_ADU_LENGTH];
    Server->HeaderLength = modbus_get_header_length(Server->Context);
    
    LaterCall = new Later(FETCH_TIMEOUT, true, &__timed_update, Server, State, Database);
    if(Server->ClientSocket == -1){
        std::cout << "Failed to listen : " << modbus_strerror(errno) << std::endl;
        return -1;
    }
    
    while(1){
        std::cout << "Listening for connection" << std::endl;
        modbus_tcp_accept(Server->Context, &Server->ClientSocket);
        (void) server_handle_connection(Server);
        std::cout << "Connection ended" << std::endl;
    }   
}

static bool exist_file(const char *name){
    return access(name, F_OK) != -1;
}

void WaitDatabase(const char *path){
    int exists = exist_file(path);
    int count = 0;
    while(!exists){
        std::cout << "Waiting for Database creation [" << count++ << "]" << std::endl;
        sleep(1);
        exists = exist_file(path);
    }
}

/*
 * Entry point
*/
int main(int argc, char **argv){
//	printf("Inicio da execução\n");
    WaitDatabase(DATABASE_PATH);

//    printf("Inicializando banco de dados\n");
    CMDatabase *Database = CMDB_new(DATABASE_PATH);
    CMState State;
    State.LinearDataLogRT = 0;

//    printf("Realizando primeira busca no banco de dados\n");
    if(!CMDB_query_state(Database, &State)){
        std::cout << "Failed to perform SQL Linearization" << std::endl;
        return -1;
    }

//    printf("Inicializando MODBUS\n");
    (void) init_modbus(Database, &State);
    
    return 0;
}
