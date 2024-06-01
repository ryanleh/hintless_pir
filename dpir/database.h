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

#ifndef HINTLESS_PIR_HINTLESS_SIMPLEPIR_DATABASE_H_
#define HINTLESS_PIR_HINTLESS_SIMPLEPIR_DATABASE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "dpir/parameters.h"
#include "lwe/types.h"

namespace hintless_pir {
namespace hintless_simplepir {

// SimplePIR database, which stores records in a matrix and allows to retrieve
// a column by computing the inner product with a selection vector.
// The database class also stores the hint matrix, which is the product of the
// database matrix and the LWE random pad "A" matrix.
// To handle long records, we shard the database according to the LWE plaintext
// modulus, and we store a hint matrix per shard.
class Database {
 public:
  // Returns an empty database supporting the given parameters.
  static absl::StatusOr<std::unique_ptr<Database>> Create(
      const Parameters& parameters);

  // Returns a database with random records for the given parameters.
  static absl::StatusOr<std::unique_ptr<Database>> CreateRandom(
      const Parameters& parameters);

  // Sets the LWE "A" matrix used by the SimplePIR protocol.
  absl::Status UpdateLweQueryPad(const lwe::Matrix* lwe_query_pad);

  void SetHint(std::vector<uint32_t>& vals);

  // Appends a record at the current end of the database.
  absl::Status Append(absl::string_view record);

  // Updates the hint matrices. This must be called before the database is
  // ready for accepting client queries, or after a new LWE query pad is set.
  absl::Status UpdateHints();

  // Returns the products between the data matrices and the query vector, one
  // per shard.
  absl::StatusOr<std::vector<lwe::Vector>> InnerProductWith(
      const lwe::Vector& query) const;

  // Accessors.
  absl::StatusOr<std::string> Record(int64_t index) const;

  absl::Span<const lwe::Matrix> Data() const { return data_matrices_; }
  absl::Span<const lwe::Matrix> Hints() const { return hint_matrices_; }

  size_t NumShards() const { return data_matrices_.size(); }
  size_t NumRecords() const { return num_records_; }

 private:
  explicit Database(Parameters params, const lwe::Matrix* lwe_query_pad,
                    int64_t num_records, std::vector<lwe::Matrix> data_matrices,
                    std::vector<lwe::Matrix> hint_matrices)
      : params_(std::move(params)),
        lwe_query_pad_(lwe_query_pad),
        num_records_(num_records),
        data_matrices_(std::move(data_matrices)),
        hint_matrices_(std::move(hint_matrices)) {}

  // Returns the row and the column indices of the given database index to store
  // a record in the data matrices.
  std::pair<int64_t, int64_t> MatrixCoordinate(int64_t index) const {
    int64_t row_idx = index / params_.db_cols;
    int64_t col_idx = index % params_.db_cols;
    return std::make_pair(row_idx, col_idx);
  }

  // The parameters of the SimplePIR protocol.
  const Parameters params_;

  // The "A" component of LWE query ciphertexts. Does not own the object.
  const lwe::Matrix* lwe_query_pad_;

  // The number of records currently in the database.
  int64_t num_records_;

  // The database matrices, one per shard of the database.
  std::vector<lwe::Matrix> data_matrices_;

  // The hint matrices, one per shard of the database.
  std::vector<lwe::Matrix> hint_matrices_;
};

}  // namespace hintless_simplepir
}  // namespace hintless_pir

#endif  // HINTLESS_PIR_HINTLESS_SIMPLEPIR_DATABASE_H_
