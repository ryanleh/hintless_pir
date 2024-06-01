
#include "dpir/dpir_ffi.h"
#include "dpir/dpir_test.h"

/*
 * Server
 */
server_t *server_new() {
    server_t *server = (server_t*)malloc(sizeof(server));
    //auto tmp = Server::Create(kParameters).value();
    //server->s = tmp.release();
    return server;
}

void server_free(server_t* server) {
    // TODO: This might not be safe
    delete server->s;
    free(server);
}

//bytes_t server_set_hint(server_t* server, ints_t matrix) {
//    // First, check that the hint size is what we expect
//    auto db = server->s->GetDatabase();
//    auto hints = db->Hints();
//    if (hints.size() != 1) {
//        std::cerr << "Invalid number of hints" << std::endl;
//    }
//
//    // Set the hint
//    auto hint = hints[0];
//    for (size_t i = 0; i < matrix.size; i++) {
//        *(hint.data() + i) = *(matrix.data + i);
//    }
//
//    // Now preprocess and grab the public parameters
//    server->s->Preprocess();
//    auto params = server->s->GetPublicParams();
//
//    // Serialize to byte object
//    size_t num_bytes = params.ByteSizeLong();
//    char* buf = new char [num_bytes];
//    params.SerializeToArray(buf, num_bytes);
//
//    return bytes_t{ .data = buf, .size = num_bytes };
//}
//
//void server_set_query(server_t* server, bytes_t msg) {
//    HintlessPirRequest request;
//    bool success = request.ParseFromArray(msg.data, msg.size);
//    if (!success) {
//        std::cerr << "Error parsing queries" << std::endl;
//    }
//    server->s->PreprocessQueries(request);
//}
//
//// TODO: Figure out how to handle data here
//bytes_t server_answer_query(server_t* server) {
//    auto response = server->s->ProcessQueries().value();
//    // Serialize to byte object
//    size_t num_bytes = response.ByteSizeLong();
//    char* buf = new char [num_bytes];
//    response.SerializeToArray(buf, num_bytes);
//
//    return bytes_t{ .data = buf, .size = num_bytes};
//}
//
///*
// * Client
// */
//client_t *new_client(bytes_t msg) {
//    HintlessPirServerPublicParams params;
//    bool success = params.ParseFromArray(msg.data, msg.size);
//    if (!success) {
//        std::cerr << "Error parsing params" << std::endl;
//    }
//
//    client_t *client = (client_t*)malloc(sizeof(client));
//    auto tmp = Client::Create(kParameters, params).value();
//    client->c = tmp.release();
//    return client;
//}
//
//void client_free(client_t* client) {
//    // TODO: This might not be safe
//    delete client->c;
//    free(client);
//}
//
//
//bytes_t client_query(client_t* client, ints_t* msg, size_t batch_size) {
//    // TODO: Don't copy here
//    std::vector<std::vector<uint32_t>> queries(batch_size);
//    for (int i = 0; i < batch_size; i++) {
//        queries[i].assign(msg[i].data, msg[i].data + msg[i].size);
//    }
//    auto proto = client->c->GenerateQuery(queries).value();
//
//    // Serialize to byte object
//    size_t num_bytes = proto.ByteSizeLong();
//    char* buf = new char [num_bytes];
//    proto.SerializeToArray(buf, num_bytes);
//    return bytes_t{ .data = buf, .size = num_bytes};
//}
//
//
//void client_recover(client_t* client, bytes_t msg, ints_t* responses) {
//    HintlessPirResponse response;
//    bool success = response.ParseFromArray(msg.data, msg.size);
//    if (!success) {
//        std::cerr << "Error parsing response" << std::endl;
//    }
//    
//    std::vector<std::vector<uint32_t>> results = client->c->RecoverInts(response).value();
//    for (int i = 0; i < results.size(); i++) {
//        uint32_t* buf = new uint32_t [results[i].size()];
//        std::copy(results[i].begin(), results[i].end(), buf);
//        *(responses+i) = ints_t{.data = buf, .size = results[i].size()};
//    }
//}
//
//
//void bytes_free(bytes_t msg) {
//    delete msg.data;
//};
//
//void ints_free(ints_t msg) {
//    delete msg.data;
//};
