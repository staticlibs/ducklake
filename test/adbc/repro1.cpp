
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

static void ExecutePrepared(duckdb::Connection &con, const std::string &query) {
    auto ps = con.Prepare(query);
    if (!ps) {
        std::cout << "Prepare fails" << std::endl;
        std::exit(1);
    }
    ps->Execute();
}

static duckdb::Connection OpenConnAndAwaitStatup(duckdb::DuckDB &db, std::atomic<int> &ready, std::atomic<bool> &start) {
    duckdb::Connection con(db);
    ready++;
    while (!start) {
        std::this_thread::yield();
    }
    return con;
}

int main() {
    duckdb::DuckDB db(nullptr);

    duckdb::Connection setup(db);
    ExecuteCheck(setup, "LOAD '/Volumes/data/projects/duck/ducklake/build/relassert/extension/ducklake/ducklake.duckdb_extension'");
    ExecuteCheck(setup, "LOAD '/Volumes/data/projects/duck/ducklake/build/relassert/extension/postgres_scanner/postgres_scanner.duckdb_extension'");
    ExecuteCheck(setup, "ATTACH 'ducklake:postgres:host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres' AS lake (DATA_PATH '/Volumes/data/projects/duck/ducklake/adbc_build/data')");
    ExecuteCheck(setup, "SET threads=1");
    ExecuteCheck(setup, "DROP SCHEMA IF EXISTS lake.app_data CASCADE");
    ExecuteCheck(setup, "CREATE SCHEMA lake.app_data");
	ExecuteCheck(setup, "CREATE TABLE lake.app_data.table_0 (item_id VARCHAR, lookup_id VARCHAR, revision_id BIGINT, status VARCHAR)");
	ExecuteCheck(setup, "INSERT INTO lake.app_data.table_0 VALUES ('item-1','lookup-1',1,'active')");
	ExecuteCheck(setup, "CREATE TABLE lake.app_data.table_other (col1 INT)");

    int duration_seconds = 15;
	std::atomic<int> ready {0};
	std::atomic<bool> start {false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);

	std::thread th1([&]{
        duckdb::Connection con = OpenConnAndAwaitStatup(db, ready, start);
		for (int i = 0; std::chrono::steady_clock::now() < deadline; i++) {
            ExecutePrepared(con, "UPDATE lake.app_data.table_0 SET revision_id=" + std::to_string(i) + " WHERE item_id='item-1'");
		}
	});

	std::thread th2([&]{
        duckdb::Connection con = OpenConnAndAwaitStatup(db, ready, start);
		for (int i = 0; std::chrono::steady_clock::now() < deadline; i++) {
            ExecutePrepared(con, "CREATE OR REPLACE TABLE lake.app_data.replaced_table AS SELECT 1 AS value");
        }
	});

	while (ready < 2) {
		std::this_thread::yield();
	}
	start = true;
	th1.join();
	th2.join();

    std::cout << "No crash reproduced" << std::endl;

    return 0;
}