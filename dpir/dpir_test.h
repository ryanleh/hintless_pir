#include "dpir/client.h"
#include "dpir/server.h"
#include "dpir/parameters.h"
#include "linpir/parameters.h"

#include "dpir/dpir_ffi.h"

using namespace hintless_pir;
using namespace hintless_pir::hintless_simplepir;
using RlweInteger = hintless_pir::hintless_simplepir::Parameters::RlweInteger;
//
const hintless_pir::hintless_simplepir::Parameters kParameters{
    .db_rows = 4096,
    .db_cols = 4096,
    .db_record_bit_size = 9,
    .batch_size = 1,
    .lwe_secret_dim = 2048,
    .lwe_modulus_bit_size = 32,
    .lwe_plaintext_bit_size = 9,
    .lwe_error_variance = 8,
    .linpir_params =
       hintless_pir::linpir::RlweParameters<RlweInteger>{
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


/*
 * C-wrapper types
 */
struct server_s {
    hintless_pir::hintless_simplepir::Server *s; 
}; 

struct client_s {
    hintless_pir::hintless_simplepir::Client *c; 
}; 
