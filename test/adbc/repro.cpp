#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "duckdb.hpp"
#include "duckdb/common/adbc/adbc.hpp"

extern "C" AdbcStatusCode duckdb_adbc_init(int version, void *driver, AdbcError *error);

namespace {

AdbcDriver driver {};
std::mutex output_mutex;

void ReleaseError(AdbcError &error) {
	if (error.release) {
		error.release(&error);
	}
	error = {};
}

std::string ErrorMessage(const AdbcError &error) {
	return error.message ? error.message : "";
}

 void RequireSuccess(AdbcStatusCode status, AdbcError &error, std::string_view operation) {
	if (status == ADBC_STATUS_OK) {
		ReleaseError(error);
		return;
	}
	std::cerr << "Setup failed during " << operation << ": " << ErrorMessage(error) << "\n";
	ReleaseError(error);
	std::exit(2);
}

AdbcStatusCode Execute(AdbcConnection &connection, const std::string &sql) {
	AdbcError error {};
	AdbcStatement statement {};
	ArrowArrayStream stream {};
	int64_t rows_affected = 0;

	auto status = driver.StatementNew(&connection, &statement, &error);
	RequireSuccess(status, error, "StatementNew");

	status = driver.StatementSetSqlQuery(&statement, sql.c_str(), &error);
	RequireSuccess(status, error, "StatementSetSqlQuery");
	status = driver.StatementExecuteQuery(&statement, &stream, &rows_affected, &error);
	if (status != ADBC_STATUS_OK) {
		std::lock_guard<std::mutex> guard(output_mutex);
		std::cout << "StatementSetSqlQuery error:" << std::endl;
		std::cout << sql << std::endl;
		std::cout << ErrorMessage(error) << std::endl;
	}
	ReleaseError(error);

	AdbcError release_error {};
	auto release_status = driver.StatementRelease(&statement, &release_error);
	ReleaseError(release_error);
	return status;
}

void ExecuteCheck(AdbcConnection &connection, const std::string &sql) {
	AdbcStatusCode status = Execute(connection, sql);
	if (status != ADBC_STATUS_OK) {
		std::lock_guard<std::mutex> guard(output_mutex);
		std::cout << "ExecuteCheck error: " << sql << std::endl;
		std::exit(1);
	}
}

} // namespace

int main(int argc, char **argv) {

	AdbcError error {};
	RequireSuccess(duckdb_adbc_init(ADBC_VERSION_1_1_0, &driver, &error), error, "driver initialization");
	AdbcDatabase database {};
	RequireSuccess(driver.DatabaseNew(&database, &error), error, "DatabaseNew");
	RequireSuccess(driver.DatabaseInit(&database, &error), error, "DatabaseInit");
	AdbcConnection setup {};
	RequireSuccess(driver.ConnectionNew(&setup, &error), error, "setup ConnectionNew");
	RequireSuccess(driver.ConnectionInit(&setup, &database, &error), error, "setup ConnectionInit");

    ExecuteCheck(setup, "LOAD '/Volumes/data/projects/duck/ducklake/build/debug/extension/ducklake/ducklake.duckdb_extension'");
    ExecuteCheck(setup, "LOAD '/Volumes/data/projects/duck/ducklake/build/debug/extension/postgres_scanner/postgres_scanner.duckdb_extension'");
    ExecuteCheck(setup, "ATTACH 'ducklake:postgres:host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres' AS lake (DATA_PATH '/Volumes/data/projects/duck/ducklake/adbc_build/data', ENCRYPTED)");
    ExecuteCheck(setup, "SET threads=1");
    ExecuteCheck(setup, "SET force_mbedtls_unsafe = 'true'");
    ExecuteCheck(setup, "DROP SCHEMA IF EXISTS lake.app_data CASCADE");
    ExecuteCheck(setup, "CREATE SCHEMA lake.app_data");
	ExecuteCheck(setup, "CREATE TABLE lake.app_data.table_0 (item_id VARCHAR, lookup_id VARCHAR, revision_id BIGINT, status VARCHAR)");
	ExecuteCheck(setup, "CREATE TABLE lake.app_data.table_1 (item_id VARCHAR, lookup_id VARCHAR, revision_id BIGINT, status VARCHAR)");
	ExecuteCheck(setup, "INSERT INTO lake.app_data.table_0 VALUES ('item-1','lookup-1',1,'active')");

	std::atomic<int> ready {0};
	std::atomic<bool> start {false};
	const int duration_seconds =  15;

	std::thread th1([&database, &ready, &start, &duration_seconds]{
		AdbcError worker_error {};
		AdbcConnection connection {};
		RequireSuccess(driver.ConnectionNew(&connection, &worker_error), worker_error, "worker ConnectionNew");
		RequireSuccess(driver.ConnectionInit(&connection, &database, &worker_error), worker_error, "worker ConnectionInit");
		ready++;
		while (!start) {
			std::this_thread::yield();
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);
		int iteration = 0;
		while (std::chrono::steady_clock::now() < deadline) {
			Execute(connection, "UPDATE lake.app_data.table_0 SET revision_id=" + std::to_string(iteration) + " WHERE item_id='item-1'");
			++iteration;
		}
		AdbcError release_error {};
		driver.ConnectionRelease(&connection, &release_error);
		ReleaseError(release_error);
	});

	std::thread th2([&database, &ready, &start, &duration_seconds]{
		AdbcError worker_error {};
		AdbcConnection connection {};
		RequireSuccess(driver.ConnectionNew(&connection, &worker_error), worker_error, "worker ConnectionNew");
		RequireSuccess(driver.ConnectionInit(&connection, &database, &worker_error), worker_error, "worker ConnectionInit");
		ready++;
		while (!start) {
			std::this_thread::yield();
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);
		int iteration = 0;
		while (std::chrono::steady_clock::now() < deadline) {
			Execute(connection, "CREATE OR REPLACE TABLE lake.app_data.replaced_table AS SELECT 1 AS value");
			++iteration;
		}
		AdbcError release_error {};
		driver.ConnectionRelease(&connection, &release_error);
		ReleaseError(release_error);
	});

	std::atomic<bool> worker_setup_failed {false};
	std::vector<std::thread> workers;

	while (ready < 2) {
		std::this_thread::yield();
	}
	start = true;
	th1.join();
	th2.join();

	AdbcError release_error {};
	driver.ConnectionRelease(&setup, &release_error);
	ReleaseError(release_error);
	driver.DatabaseRelease(&database, &release_error);
	ReleaseError(release_error);

	std::cerr << "NOT REPRODUCED: no database-invalidated error was observed in this attempt\n";
	return 0;
}
