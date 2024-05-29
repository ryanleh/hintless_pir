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

#include <cstdint>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "dpir/client.h"
#include "dpir/database.h"
#include "dpir/parameters.h"
#include "dpir/server.h"
#include "linpir/parameters.h"
#include "shell_encryption/testing/status_testing.h"

ABSL_FLAG(int, num_rows, 1024, "Number of rows");
ABSL_FLAG(int, num_cols, 1024, "Number of cols");

namespace hintless_pir {
namespace hintless_simplepir {
namespace {

using RlweInteger = Parameters::RlweInteger;

const Parameters kParameters{
    .db_rows = 8192,
    .db_cols = 8192,
    .db_record_bit_size = 9,
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

void BM_HintCompr(benchmark::State& state) {
  Parameters params = kParameters;

  // Create server and fill in random database records.
  auto server = Server::CreateWithRandomDatabaseRecords(params).value();
  const Database* database = server->GetDatabase();
  ASSERT_EQ(database->NumRecords(), params.db_rows * params.db_cols);

  // Preprocess the server and get public parameters.
  ASSERT_OK(server->Preprocess());
  auto public_params = server->GetPublicParams();

  // Create a client and issue request.
  auto client = Client::Create(params, public_params).value();
  auto request = client->GenerateRequest(1).value();

  for (auto _ : state) {
    server->PreprocessQuery(request);
    auto response = server->ProcessQuery(request);
    //auto response = server->HandleRequest(request);
    benchmark::DoNotOptimize(response);
  }

  // Print size of the response
  auto response = server->ProcessQuery(request).value();
  std::cout << "Response size: " << response.ByteSize() / (1 << 10) << "KB" << std::endl;

  // Sanity check on the correctness of the instantiation.
  std::string record = client->RecoverRecord(response).value();
  std::string expected = database->Record(1).value();
  ASSERT_EQ(record, expected);
}
BENCHMARK(BM_HintCompr)->Unit(benchmark::kMillisecond);

void BM_HintPreprocess(benchmark::State& state) {
  Parameters params = kParameters;

  // Create server and fill in random database records.
  auto server = Server::CreateWithRandomDatabaseRecords(params).value();
  const Database* database = server->GetDatabase();
  ASSERT_EQ(database->NumRecords(), params.db_rows * params.db_cols);

  // Preprocess the server and get public parameters.
  for (auto _ : state) {
      server->Preprocess();
  }
}
BENCHMARK(BM_HintPreprocess)->Unit(benchmark::kSecond);


}  // namespace
}  // namespace hintless_simplepir
}  // namespace hintless_pir

// Declare benchmark_filter flag, which will be defined by benchmark library.
// Use it to check if any benchmarks were specified explicitly.
//
namespace benchmark {
extern std::string FLAGS_benchmark_filter;
}
using benchmark::FLAGS_benchmark_filter;

int main(int argc, char* argv[]) {
  FLAGS_benchmark_filter = "BM_HintCompr";
  benchmark::Initialize(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  if (!FLAGS_benchmark_filter.empty()) {
    benchmark::RunSpecifiedBenchmarks();
  }
  benchmark::Shutdown();
  return 0;
}
