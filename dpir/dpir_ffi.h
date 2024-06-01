#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>


// These guards are needed for CGO
#ifdef __cplusplus
extern "C" {
#endif
    /*
     * ======== Structs ========
     */
    
    // Lazily defined so that we don't need to link to SEAL at runtime
    typedef struct server_s server_t;
    typedef struct client_s client_t;

    typedef struct {
        char* data;
        size_t size;
    } bytes_t;

    typedef struct {
        uint32_t* data;
        size_t size;
    } ints_t;


    /*
     * ======== API ========
     */
    
    // Server functions
    server_t *server_new();
    void server_free(server_t*);
//   
//    bytes_t server_set_hint(server_t*, ints_t);
//    void server_set_query(server_t*, bytes_t);
//    bytes_t server_answer_query(server_t*);
//
//    // Client functions
//    client_t *client_new(bytes_t msg);
//    void client_free(client_t*);
//
//    bytes_t client_query(client_t*, ints_t msg);
//    ints_t client_recover(client_t*, bytes_t msg);
//
//    // Free a message (only call if made in C)
//    void bytes_free(bytes_t);
//    void ints_free(ints_t);


#ifdef __cplusplus
}
#endif
