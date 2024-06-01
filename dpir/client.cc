// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "dpir/client.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Eigen/Core"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "dpir/parameters.h"
#include "dpir/serialization.pb.h"
#include "dpir/utils.h"
#include "lwe/encode.h"
#include "lwe/lwe_symmetric_encryption.h"
#include "lwe/types.h"
#include "shell_encryption/int256.h"
#include "shell_encryption/prng/prng.h"
#include "shell_encryption/prng/single_thread_chacha_prng.h"
#include "shell_encryption/prng/single_thread_hkdf_prng.h"
#include "shell_encryption/rns/crt_interpolation.h"
#include "shell_encryption/status_macros.h"

namespace hintless_pir {
namespace hintless_simplepir {

absl::StatusOr<std::unique_ptr<Client>> Client::Create(
    const Parameters& params,
    const HintlessPirServerPublicParams& public_params) {
  if (!(params.prng_type == rlwe::PRNG_TYPE_HKDF ||
        params.prng_type == rlwe::PRNG_TYPE_CHACHA)) {
    return absl::InvalidArgumentError("Invalid PRNG type in `params`.");
  }
  // Create LinPir clients, one per plaintext modulus in `ts`.
  auto const& rlwe_params = params.linpir_params;
  int num_linpir_instances = rlwe_params.ts.size();
  if (public_params.prng_seed_linpir_ct_pads_size() != num_linpir_instances) {
    return absl::InvalidArgumentError(
        "`public_params` contains incorrect number of PRNG seeds.");
  }
  std::vector<std::unique_ptr<const RlweRnsContext>> rlwe_contexts;
  std::vector<std::unique_ptr<LinPirClient>> linpir_clients;
  rlwe_contexts.reserve(num_linpir_instances);
  linpir_clients.reserve(num_linpir_instances);
  for (int i = 0; i < num_linpir_instances; ++i) {
    RLWE_ASSIGN_OR_RETURN(
        auto rlwe_context,
        RlweRnsContext::CreateForBfvFiniteFieldEncoding(
            rlwe_params.log_n, rlwe_params.qs, /*ps=*/{}, rlwe_params.ts[i]));
    auto rlwe_context_ptr =
        std::make_unique<const RlweRnsContext>(std::move(rlwe_context));
    RLWE_ASSIGN_OR_RETURN(
        auto linpir_client,
        LinPirClient::Create(rlwe_params, rlwe_context_ptr.get(),
                             public_params.prng_seed_linpir_ct_pads(i),
                             public_params.prng_seed_linpir_gk_pad()));
    rlwe_contexts.push_back(std::move(rlwe_context_ptr));
    linpir_clients.push_back(std::move(linpir_client));
  }
  std::vector<const RlwePrimeModulus*> rlwe_moduli =
      rlwe_contexts[0]->MainPrimeModuli();

  // Create another RNS context for computing CRT interpolation wrt plaintext
  // moduli. We set its plaintext modulus to 2 as a place holder.
  RLWE_ASSIGN_OR_RETURN(
      RlweRnsContext crt_context,
      RlweRnsContext::Create(rlwe_params.log_n, rlwe_params.ts, /*ps=*/{}, 2));

  return absl::WrapUnique(
      new Client(params, public_params.prng_seed_lwe_query_pad(),
                 std::move(rlwe_contexts), std::move(rlwe_moduli),
                 std::move(linpir_clients), std::move(crt_context)));
}

absl::StatusOr<HintlessPirRequest> Client::GenerateRequest(std::vector<int64_t> indices) {
  // Step 1. Encrypting the selection vector under LWE.
  lwe::Matrix lwe_pad;
  std::unique_ptr<rlwe::SecurePrng> lwe_enc_prng;
  std::string prng_seed_linpir_sk;
  if (params_.prng_type == rlwe::PRNG_TYPE_HKDF) {
    RLWE_ASSIGN_OR_RETURN(auto pad_prng, rlwe::SingleThreadHkdfPrng::Create(
                                             prng_seed_lwe_query_pad_));
    RLWE_ASSIGN_OR_RETURN(
        lwe_pad, lwe::ExpandPad(params_.db_cols, params_.lwe_secret_dim,
                                pad_prng.get()));
    RLWE_ASSIGN_OR_RETURN(std::string prng_seed_enc,
                          rlwe::SingleThreadHkdfPrng::GenerateSeed());
    RLWE_ASSIGN_OR_RETURN(lwe_enc_prng,
                          rlwe::SingleThreadHkdfPrng::Create(prng_seed_enc));
    RLWE_ASSIGN_OR_RETURN(prng_seed_linpir_sk,
                          rlwe::SingleThreadHkdfPrng::GenerateSeed());

  } else {
    RLWE_ASSIGN_OR_RETURN(auto pad_prng, rlwe::SingleThreadChaChaPrng::Create(
                                             prng_seed_lwe_query_pad_));
    RLWE_ASSIGN_OR_RETURN(
        lwe_pad, lwe::ExpandPad(params_.db_cols, params_.lwe_secret_dim,
                                pad_prng.get()));
    RLWE_ASSIGN_OR_RETURN(std::string prng_seed_enc,
                          rlwe::SingleThreadChaChaPrng::GenerateSeed());
    RLWE_ASSIGN_OR_RETURN(lwe_enc_prng,
                          rlwe::SingleThreadChaChaPrng::Create(prng_seed_enc));
    RLWE_ASSIGN_OR_RETURN(prng_seed_linpir_sk,
                          rlwe::SingleThreadChaChaPrng::GenerateSeed());
  }

  // Choosing the largest scaling factor that supports our plaintext space
  int log_scaling_factor =
      params_.lwe_modulus_bit_size - params_.lwe_plaintext_bit_size;

  // Plaintext is a selection vector for col_idx
  std::vector<int64_t> row_idx(indices.size());
  std::vector<int64_t> col_idx(indices.size());
  std::vector<lwe::Vector> lwe_secrets(indices.size());
  std::vector<SerializedLweCiphertext> queries(indices.size());
  for (size_t i = 0; i < indices.size(); i++) {
      if (indices[i] < 0 || indices[i] >= params_.db_rows * params_.db_cols) {
        return absl::InvalidArgumentError("`index` out of range.");
      }
      row_idx.push_back(indices[i] / params_.db_cols);
      col_idx.push_back(indices[i] % params_.db_cols);
     
      // Encrypt each query using the same key and different a
      RLWE_ASSIGN_OR_RETURN(
          lwe::SymmetricLweKey lwe_secret_key,
          lwe::SymmetricLweKey::Sample(params_.lwe_secret_dim, lwe_enc_prng.get()));

      lwe::Vector query_vector = lwe::Vector::Zero(params_.db_cols);
      query_vector[col_idx.back()] = 1;
      RLWE_RETURN_IF_ERROR(lwe_secret_key.EncryptFromPadInPlace(
          query_vector, lwe_pad, log_scaling_factor, lwe_enc_prng.get()));
      queries[i] = SerializeLweCiphertext(query_vector);

      lwe_secrets[i] = lwe_secret_key.Key();
  }

  // Cache the per request state.
  state_ = ClientState{.row_idx = row_idx,
                       .col_idx = col_idx,
                       .prng_seed_linpir_sk = std::move(prng_seed_linpir_sk)};
  batch_size_ = indices.size();

  HintlessPirRequest request;
  *request.mutable_ct_query_vector() = {queries.begin(), queries.end()};

  // Step 2. Encrypting the LWE secret using LinPir.
  RLWE_RETURN_IF_ERROR(GenerateLinPirRequestInPlace(request, lwe_secrets));
  return request;
}

absl::StatusOr<HintlessPirRequest> Client::GenerateQuery(std::vector<std::vector<uint32_t>> keys) {
  // Step 1. Encrypting the selection vector under LWE.
  std::string prng_seed_linpir_sk;
  if (params_.prng_type == rlwe::PRNG_TYPE_HKDF) {
    RLWE_ASSIGN_OR_RETURN(prng_seed_linpir_sk,
                          rlwe::SingleThreadHkdfPrng::GenerateSeed());

  } else {
    RLWE_ASSIGN_OR_RETURN(prng_seed_linpir_sk,
                          rlwe::SingleThreadChaChaPrng::GenerateSeed());
  }

  // Plaintext is a selection vector for col_idx
  std::vector<lwe::Vector> lwe_secrets(keys.size());
  for (size_t i = 0; i < keys.size(); i++) {
      lwe_secrets[i] = lwe::Vector::Zero(keys[i].size());
      for (size_t j = 0; j < keys[i].size(); j++) {
          lwe_secrets[i][j] = keys[i][j];
      }
  }

  // Cache the per request state.
  state_ = ClientState{.row_idx = {},
                       .col_idx = {},
                       .prng_seed_linpir_sk = std::move(prng_seed_linpir_sk)};
  batch_size_ = keys.size();

  // Step 2. Encrypting the LWE secret using LinPir.
  HintlessPirRequest request;
  RLWE_RETURN_IF_ERROR(GenerateLinPirRequestInPlace(request, lwe_secrets));
  return request;
}

absl::Status Client::GenerateLinPirRequestInPlace(
    HintlessPirRequest& request,
    const std::vector<lwe::Vector>& lwe_secrets) const {
  if (linpir_clients_.empty()) {
    return absl::InvalidArgumentError("No LinPir client available.");
  }

  // Encode the LWE secret vector using LinPir plaintext moduli, and also
  // generate a GaloisKey which is shared by all LinPir requests.
  RlweInteger lwe_modulus = RlweInteger{1} << params_.lwe_modulus_bit_size;
    
  for (int k = 0; k < linpir_clients_.size(); ++k) {
      RlweInteger plaintext_modulus = rlwe_contexts_[k]->PlaintextModulus();

      std::vector<std::vector<RlweInteger>> lwe_secrets_mod_t(lwe_secrets.size());
      for (int i = 0; i < lwe_secrets.size(); i++) {
        lwe_secrets_mod_t[i] = EncodeLweVector(lwe_secrets[i], lwe_modulus, plaintext_modulus);
      }
     
      RLWE_ASSIGN_OR_RETURN(
          auto ct, linpir_clients_[k]->EncryptQuery(lwe_secrets_mod_t,
                                                    state_.prng_seed_linpir_sk));

      for (int i = 0; i < ct.size(); i++) {
          RLWE_ASSIGN_OR_RETURN(auto ct_b, ct[i].Component(0));
          RLWE_ASSIGN_OR_RETURN(*request.add_linpir_ct_bs(),
                                ct_b.Serialize(rlwe_moduli_));
      }
  }

  RLWE_ASSIGN_OR_RETURN(auto gk, linpir_clients_[0]->GenerateGaloisKey(
                                     state_.prng_seed_linpir_sk));
  for (auto const& gk_b : gk.GetKeyB()) {
    RLWE_ASSIGN_OR_RETURN(*request.add_linpir_gk_bs(),
                          gk_b.Serialize(rlwe_moduli_));
  }
  return absl::OkStatus();
}

std::vector<Client::RlweInteger> Client::EncodeLweVector(
    const lwe::Vector& lwe_vector, RlweInteger lwe_modulus,
    RlweInteger encode_modulus) {
  RlweInteger lwe_modulus_half = lwe_modulus >> 1;
  std::vector<RlweInteger> lwe_vector_mod_t(lwe_vector.size(), 0);
  for (int i = 0; i < lwe_vector.size(); ++i) {
    RlweInteger x = lwe_vector[i];
    lwe_vector_mod_t[i] =
        ConvertModulus(x, lwe_modulus, encode_modulus, lwe_modulus_half);
  }
  return lwe_vector_mod_t;
}

absl::StatusOr<std::vector<std::string>> Client::RecoverRecord(const HintlessPirResponse& response) {
  int num_shards =
      DivAndRoundUp(params_.db_record_bit_size, params_.lwe_plaintext_bit_size);
  if (response.ct_records_size() != num_shards * batch_size_) {
    return absl::InvalidArgumentError("`response` has incorrect size.");
  }

  // Recover decryption_parts = Hint * LWE secret = Database * A * LWE secret.
  RLWE_ASSIGN_OR_RETURN(std::vector<std::vector<lwe::Vector>> decryption_parts,
                        RecoverLweDecryptionParts(response));

  // Decrypt the LWE ciphertexts in response.
  auto ct_per = response.ct_records_size() / batch_size_;
  std::vector<std::string> results(batch_size_);
  for (int i = 0; i < batch_size_; i++) {
      std::vector<lwe::Integer> values;
      values.reserve(ct_per);
      for (int j = 0; j < ct_per; ++j) {
        lwe::Vector ct_records = DeserializeLweCiphertext(response.ct_records(i * ct_per + j));
        if (ct_records.rows() != params_.db_rows) {
          return absl::InvalidArgumentError(absl::StrCat(
              "The server response has incorrect dimension; got ",
              ct_records.rows(), " but expecting ", params_.db_rows, "."));
        }

        // Remove hint * s from the server response, which gives us \Delta * m + e.
        lwe::Vector noisy_plaintext = ct_records.row(state_.row_idx[i]);
        noisy_plaintext[0] -= decryption_parts[i][j][state_.row_idx[i]];

        // Remove the error e.
        int log_scaling_factor =
            params_.lwe_modulus_bit_size - params_.lwe_plaintext_bit_size;
        RLWE_RETURN_IF_ERROR(
            lwe::RemoveErrorInPlace(noisy_plaintext, log_scaling_factor));

        // Extracting the coefficient from the 1 x 1 matrix noisy_plaintext.
        values.push_back(noisy_plaintext.eval()(0));
      }
      results[i] = ReconstructRecord(values, params_);
  }
  return results;
}

absl::StatusOr<std::vector<std::vector<uint32_t>>> Client::RecoverInts(const HintlessPirResponse& response) {
  int num_shards =
      DivAndRoundUp(params_.db_record_bit_size, params_.lwe_plaintext_bit_size);
  // Recover decryption_parts = Hint * LWE secret = Database * A * LWE secret.
  RLWE_ASSIGN_OR_RETURN(std::vector<std::vector<lwe::Vector>> decryption_parts,
                        RecoverLweDecryptionParts(response));

  std::vector<std::vector<uint32_t>> results(batch_size_);
  for (int i = 0; i < batch_size_; i++) {
    for (int j = 0; j < decryption_parts[i].size(); j++) {
        results[i].insert(results[i].end(), decryption_parts[i][j].begin(), decryption_parts[i][j].end());
    }
  }
  return results;
}

absl::StatusOr<std::vector<std::vector<lwe::Vector>>> Client::RecoverLweDecryptionParts(
    const HintlessPirResponse& response) const {
  using BigInteger = rlwe::uint256;

  auto plaintext_moduli = crt_context_.MainPrimeModuli();
  int num_linpir_plaintext_moduli = plaintext_moduli.size();
  if (response.linpir_responses_size() != num_linpir_plaintext_moduli * batch_size_) {
    return absl::InvalidArgumentError(
        "`response` contains unexpected number of LinPir responses.");
  }

  int num_shards =
      DivAndRoundUp(params_.db_record_bit_size, params_.lwe_plaintext_bit_size);
  if (num_shards != response.linpir_responses(0).ct_inner_products_size()) {
    return absl::InvalidArgumentError(
        "`response` contains an expected number of shards.");
  }

  std::vector<std::vector<lwe::Vector>> results(batch_size_);
  for (int i = 0; i < batch_size_; i++) {
      // CRT decomposed values of Hint * LWE secrets, where the first dimension
      // is per database shard, then per CRT modulus, and then per block in the
      // shard.
      std::vector<std::vector<std::vector<RlweModularInt>>> hint_crt_values(
          num_shards);
      for (auto& h : hint_crt_values) {
        h.resize(num_linpir_plaintext_moduli);
      }
      for (int k = 0; k < num_linpir_plaintext_moduli; ++k) {
        RLWE_ASSIGN_OR_RETURN(
            auto hint_values_mod_tk,
            linpir_clients_[k]->Recover(response.linpir_responses(k * batch_size_ + i)));
        auto mod_params_tk = plaintext_moduli[k]->ModParams();
        for (int j = 0; j < num_shards; ++j) {
          hint_crt_values[j][k].reserve(hint_values_mod_tk[j].size());
          for (auto const& hint_value : hint_values_mod_tk[j]) {
            RLWE_ASSIGN_OR_RETURN(auto hint_mod_tk, RlweModularInt::ImportInt(
                                                        hint_value, mod_params_tk));
            hint_crt_values[j][k].push_back(std::move(hint_mod_tk));
          }
        }
      }

      // CRT interpolates to get Hint * LWE secrets as balanced mod-t values, where
      // t is the product of plaintext moduli. Then convert them wrt LWE modulus.
      BigInteger p = 1;
      for (auto pi : plaintext_moduli) {
        p *= rlwe::ConvertToBigInteger<RlweInteger, BigInteger>(pi->Modulus());
      }
      BigInteger p_half = p / 2;
      BigInteger lwe_modulus = BigInteger(1) << params_.lwe_modulus_bit_size;
      std::vector<BigInteger> p_hats =
          rlwe::RnsModulusComplements<RlweModularInt, BigInteger>(plaintext_moduli);
      RLWE_ASSIGN_OR_RETURN(
          std::vector<RlweModularInt> p_hat_invs,
          crt_context_.MainPrimeModulusCrtFactors(num_linpir_plaintext_moduli - 1));

      std::vector<lwe::Vector> hint_vectors;
      hint_vectors.reserve(num_shards);
      for (int j = 0; j < num_shards; ++j) {
        RLWE_ASSIGN_OR_RETURN(
            std::vector<BigInteger> hint_values,
            (rlwe::CrtInterpolation<RlweModularInt, BigInteger>(
                hint_crt_values[j], plaintext_moduli, p_hats, p_hat_invs)));

        lwe::Vector hint = lwe::Vector::Zero(hint_values.size());
        for (int m = 0; m < hint_values.size(); ++m) {
          BigInteger x = hint_values[m] % p;
          hint[m] =
              static_cast<RlweInteger>(ConvertModulus(x, p, lwe_modulus, p_half));
        }
        hint_vectors.push_back(std::move(hint));
      }
      results[i] = hint_vectors;
  }
  return results;
}

}  // namespace hintless_simplepir
}  // namespace hintless_pir
