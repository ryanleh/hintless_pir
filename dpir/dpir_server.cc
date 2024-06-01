#include <cstdint>
#include <memory>
#include <string>

#include "dpir/socket.h"

#include "dpir/parameters.h"
#include "dpir/server.h"
#include "linpir/parameters.h"

namespace hintless_pir {
namespace hintless_simplepir {
namespace {

using RlweInteger = Parameters::RlweInteger;

const Parameters kParameters{
    .db_rows = 4096,
    .db_cols = 4096,
    .db_record_bit_size = 9,
    .batch_size = 1,
    .lwe_secret_dim = 2048,
    .lwe_modulus_bit_size = 32,
    .lwe_plaintext_bit_size = 9,
    .lwe_error_variance = 8,
    .linpir_params =
        linpir::RlweParameters<RlweInteger>{
            .log_n = 12,
            .qs = {35184371884033ULL, 35184371703809ULL},  // 90 bits
            .ts = {2056193, 1990657},                      // 42 bits
            .gadget_log_bs = {16, 16},
            .error_variance = 8,
            .prng_type = rlwe::PRNG_TYPE_HKDF,
            .rows_per_block = 1024,
        },
    .prng_type = rlwe::PRNG_TYPE_HKDF,
};

int run_server() {
    // Connect to socket
    Socket socket;
    if(socket.Connect(SERVER_SOCKET)) {
        std::cerr << "Error connecting" << std::endl;
        return -1;
    };

    // Wait for parameters
    auto params = kParameters;
    auto dims = socket.RecvUints();
    params.db_rows = dims[0];
    params.db_cols = dims[1];
    std::cout << "got dims: " << dims[0] << ", " << dims[1] << std::endl;

    // Initialize server
    auto server = Server::Create(params).value();

    // Receive hint and set on server
    auto hint_vals = socket.RecvUints();
    server->GetDatabase()->SetHint(hint_vals);
    
    // Preprocess and grab the public parameters
    if (!server->Preprocess().ok()) {
        std::cout << "Error preprocessing" << std::endl;
    }
    auto public_params = server->GetPublicParams();
    
    // Serialize to byte object
    size_t num_bytes = public_params.ByteSizeLong();
    std::vector<char> serialized(num_bytes);
    public_params.SerializeToArray(serialized.data(), num_bytes);
    
    // Send to the host
    socket.SendBytes(serialized);

    std::cout << "Server preprocessed!" << std::endl;
    
    // Receive query from client and forward to server
    auto query_ser = socket.RecvBytes();
    
    HintlessPirRequest query;
    bool success = query.ParseFromArray(query_ser.data(), query_ser.size());
    if (!success) {
        std::cerr << "Error parsing query" << std::endl;
    }
    server->PreprocessQueries(query);
    std::cout << "Server processed query!" << std::endl;
   
    // Wait for query requests
    while (true) {
        auto request = socket.RecvBytes();
        if (request[0] == 1) {
            auto answer = server->ProcessQueries().value();
            size_t num_bytes = answer.ByteSizeLong();
            std::vector<char> serialized(num_bytes);
            answer.SerializeToArray(serialized.data(), num_bytes);
            socket.SendBytes(serialized);
        } else {
            break;
        }
    }
    
    // Answer any aubsequent queries
    return 0;
}

}  // namespace
}  // namespace hintless_simplepir
}  // namespace hintless_pir



int main(int argc, char* argv[]) {
    hintless_pir::hintless_simplepir::run_server();
}
