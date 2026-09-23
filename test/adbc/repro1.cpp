
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

#include "duckdb.hpp"

static void ExecuteCheck(duckdb::Connection &con, const std::string &query) {
    auto result = con.Query(query);
    if (result->HasError()) {
        std::cerr << "Query Error: " << result->GetError() << std::endl;
        std::exit(1);
    }
}

int main() {
    duckdb::DuckDB db(nullptr);

    duckdb::Connection setup(db);
    ExecuteCheck(setup, "LOAD '../build/relassert/extension/ducklake/ducklake.duckdb_extension'");
    ExecuteCheck(setup, "LOAD '../build/relassert/extension/postgres_scanner/postgres_scanner.duckdb_extension'");
    ExecuteCheck(setup, "ATTACH 'ducklake:postgres:host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres' AS lake (DATA_PATH '/Volumes/data/projects/duck/ducklake/adbc_build/data', ENCRYPTED)");
    ExecuteCheck(setup, "SET threads=1");
    ExecuteCheck(setup, "SET force_mbedtls_unsafe = 'true'");
    ExecuteCheck(setup, "DROP SCHEMA IF EXISTS lake.app_data CASCADE");
    ExecuteCheck(setup, "CREATE SCHEMA lake.app_data");
	ExecuteCheck(setup, "CREATE TABLE lake.app_data.table_0 (item_id VARCHAR, lookup_id VARCHAR, revision_id BIGINT, status VARCHAR)");
	ExecuteCheck(setup, "CREATE TABLE lake.app_data.table_1 (item_id VARCHAR, lookup_id VARCHAR, revision_id BIGINT, status VARCHAR)");
	ExecuteCheck(setup, "INSERT INTO lake.app_data.table_0 VALUES ('item-1','lookup-1',1,'active')");

    int duration_seconds = 15;
	std::atomic<int> ready {0};
	std::atomic<bool> start {false};

	std::thread th1([&db, &ready, &start, &duration_seconds]{
        duckdb::Connection con(db);
        con.SetAutoCommit(true);
		ready++;
		while (!start) {
			std::this_thread::yield();
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);
		int iteration = 0;
		while (std::chrono::steady_clock::now() < deadline) {
			con.Query("UPDATE lake.app_data.table_0 SET revision_id=" + std::to_string(iteration) + " WHERE item_id='item-1'");
			++iteration;
		}
	});

	std::thread th2([&db, &ready, &start, &duration_seconds]{
        duckdb::Connection con(db);
        con.SetAutoCommit(true);
		ready++;
		while (!start) {
			std::this_thread::yield();
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);
		int iteration = 0;
		while (std::chrono::steady_clock::now() < deadline) {
			con.Query("CREATE OR REPLACE TABLE lake.app_data.replaced_table AS SELECT 1 AS value");
			++iteration;
		}
	});

	std::atomic<bool> worker_setup_failed {false};
	std::vector<std::thread> workers;

	while (ready < 2) {
		std::this_thread::yield();
	}
	start = true;
	th1.join();
	th2.join();

    std::cout << "No crash reproduced" << std::endl;

    return 0;
}