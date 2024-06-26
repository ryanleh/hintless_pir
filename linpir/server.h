/*
 * Copyright 2024 Google LLC.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HINTLESS_PIR_LINPIR_SERVER_H_
#define HINTLESS_PIR_LINPIR_SERVER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "linpir/database.h"
#include "linpir/parameters.h"
#include "linpir/serialization.pb.h"
#include "shell_encryption/montgomery.h"
#include "shell_encryption/rns/rns_bfv_ciphertext.h"
#include "shell_encryption/rns/rns_context.h"
#include "shell_encryption/rns/rns_error_params.h"
#include "shell_encryption/rns/rns_gadget.h"
#include "shell_encryption/rns/rns_galois_key.h"
#include "shell_encryption/rns/rns_modulus.h"
#include "shell_encryption/rns/rns_polynomial.h"

namespace hintless_pir {
namespace linpir {

// This class implements the server component of the LinPIR scheme, computing
// homomorphically the matrix-vector product between the database and the
// encrypted query vector.
// Note: When used to implement HintlessPIR, there should be an instance of
// Server per plaintext CRT modulus. Furthermore, all Server instances should
// share the same PRNG seed for the Galois key but different PRNG seeds for the
// ciphertext "a" component.
template <typename RlweInteger>
class Server {
 public:
  using ModularInt = rlwe::MontgomeryInt<RlweInteger>;
  using RnsContext = rlwe::RnsContext<ModularInt>;
  using RnsGadget = rlwe::RnsGadget<ModularInt>;
  using RnsGaloisKey = rlwe::RnsGaloisKey<ModularInt>;
  using RnsPolynomial = rlwe::RnsPolynomial<ModularInt>;
  using RnsCiphertext = rlwe::RnsBfvCiphertext<ModularInt>;
  using RnsErrorParams = rlwe::RnsErrorParams<ModularInt>;
  using PrimeModulus = rlwe::PrimeModulus<ModularInt>;

  // Creates a LinPIR server which holds the matrices stored in the databases.
  // The server holds freshly generated PRNG seeds for the "a" components of
  // query ciphertexts and Galois automorphism keys.
  static absl::StatusOr<std::unique_ptr<Server>> Create(
      const RlweParameters<RlweInteger>& parameters,
      const RnsContext* rns_context,
      const std::vector<Database<RlweInteger>*>& databases);

  // Creates a LinPIR server with the given PRNG seeds. This could be useful
  // when multiple LinPIR instances are sharing the same Galois automorphism
  // key and hence the relevant preprocessing data generated by the PRNG seeds,
  // where each instance works on a CRT modulus of the plaintext computation.
  static absl::StatusOr<std::unique_ptr<Server>> Create(
      const RlweParameters<RlweInteger>& parameters,
      const RnsContext* rns_context,
      const std::vector<Database<RlweInteger>*>& databases,
      absl::string_view prng_seed_ct_pad, absl::string_view prng_seed_gk_pad);

  // Preprocess the ciphertext automorphisms and database inner products.
  absl::Status Preprocess();

  // Process a serialized LinPir request.
  // This variant requires the server and the database are preprocessed.
  absl::StatusOr<LinPirResponse> HandleRequest(
      const LinPirRequest& request) const {
    return HandleRequest(request.ct_query_b(), request.gk_key_bs());
  }


  // TODO
  absl::Status PreprocessRequest(
    std::vector<rlwe::SerializedRnsPolynomial>& ct_query_bs,
    const google::protobuf::RepeatedPtrField<rlwe::SerializedRnsPolynomial>& proto_gk_key_bs
  );
  
  absl::StatusOr<std::vector<LinPirResponse>> ProcessRequest();

  // Process a LinPir request represented by individual protos.
  // This variant requires the server and the database are preprocessed.
  absl::StatusOr<LinPirResponse> HandleRequest(
      const rlwe::SerializedRnsPolynomial& proto_ct_query_b,
      const google::protobuf::RepeatedPtrField<rlwe::SerializedRnsPolynomial>&
          proto_gk_key_bs) const;

  // Process a LinPir request represented by a ciphertext encrypting the vector
  // and a Galois automorphism key.
  // This variant does not require preprocessing.
  absl::StatusOr<LinPirResponse> HandleRequest(const RnsCiphertext& ct_query,
                                               const RnsGaloisKey& gk) const;

  // Accessors to the PRNG seeds for generating a LinPir request.
  absl::string_view PrngSeedForCiphertextRandomPads() const {
    return prng_seed_ct_pad_;
  }
  absl::string_view PrngSeedForGaloisKeyRandomPads() const {
    return prng_seed_gk_pad_;
  }

 private:
  explicit Server(RlweParameters<RlweInteger> params,
                  std::string prng_seed_ct_pad, std::string prng_seed_gk_pad,
                  const RnsContext* rns_context,
                  std::vector<const PrimeModulus*> rns_moduli,
                  RnsGadget rns_gadget, RnsErrorParams rns_error_params,
                  std::vector<Database<RlweInteger>*> databases)
      : params_(std::move(params)),
        prng_seed_ct_pad_(std::move(prng_seed_ct_pad)),
        prng_seed_gk_pad_(std::move(prng_seed_gk_pad)),
        rns_context_(rns_context),
        rns_moduli_(std::move(rns_moduli)),
        rns_error_params_(std::move(rns_error_params)),
        rns_gadget_(std::move(rns_gadget)),
        databases_(std::move(databases)) {}

  const RlweParameters<RlweInteger> params_;

  std::string prng_seed_ct_pad_;
  std::string prng_seed_gk_pad_;

  const RnsContext* rns_context_;
  const std::vector<const PrimeModulus*> rns_moduli_;
  const RnsGadget rns_gadget_;
  const RnsErrorParams rns_error_params_;

  // Holding the matrices via mutable pointers to perform preprocessing tasks.
  std::vector<Database<RlweInteger>*> databases_;

  // Preprocessed polynomials to be used in `HandleRequest`.
  std::vector<RnsPolynomial> ct_pads_;
  std::vector<std::vector<RnsPolynomial>> ct_sub_pad_digits_;
  std::vector<RnsPolynomial> gk_pads_;

  // TODO: Test stuff
  std::vector<std::vector<RnsCiphertext>> rotated_queries_;
  std::unique_ptr<RnsGaloisKey> gk_;
  size_t batch_size_;
};

}  // namespace linpir
}  // namespace hintless_pir

#endif  // HINTLESS_PIR_LINPIR_SERVER_H_
