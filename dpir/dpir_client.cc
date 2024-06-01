#include <cstdint>
#include <memory>
#include <string>

#include "dpir/socket.h"

#include "dpir/parameters.h"
#include "dpir/client.h"
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

int run_client() {
    // Connect to the socket
    Socket socket;
    if(socket.Connect(CLIENT_SOCKET)) {
        std::cerr << "Error connecting" << std::endl;
        return -1;
    };
    
    // Wait for parameters
    auto params = kParameters;
    auto dims = socket.RecvUints();
    params.db_rows = dims[0];
    params.db_cols = dims[1];
    params.batch_size = dims[2];

    auto bytes = socket.RecvBytes();
    HintlessPirServerPublicParams public_params;
    bool success = public_params.ParseFromArray(bytes.data(), bytes.size());
    if (!success) {
        std::cerr << "Error parsing public_params" << std::endl;
    }

    // Initialize Client
    auto client = Client::Create(params, public_params).value();

    std::cout << "Client created!" << std::endl;

    while (true) {
        // Wait for keys and generate query
        std::vector<std::vector<uint32_t>> keys(params.batch_size);
        for (int i = 0; i < keys.size(); i++) {
            keys[i] = socket.RecvUints();
        }

        auto proto = client->GenerateQuery(keys).value();
        
        // Serialize to byte object
        size_t num_bytes = proto.ByteSizeLong();
        std::vector<char> serialized(num_bytes);
        proto.SerializeToArray(serialized.data(), num_bytes);
        socket.SendBytes(serialized);
    
        std::cout << "Client generated query!" << std::endl;

        // Wait for response
        auto bytes = socket.RecvBytes();
        HintlessPirResponse response;
        bool success = response.ParseFromArray(bytes.data(), bytes.size());
        if (!success) {
            std::cerr << "Error parsing response" << std::endl;
        }

        // Send the final result back to host
        auto results = client->RecoverInts(response).value();
        for (int i = 0; i < results.size(); i++) {
            socket.SendUints(results[i]);
        }

        std::cout << "Client produced answer!" << std::endl;
    }
    return 0;
}


}  // namespace
}  // namespace hintless_simplepir
}  // namespace hintless_pir



int main(int argc, char* argv[]) {
    hintless_pir::hintless_simplepir::run_client();
}
