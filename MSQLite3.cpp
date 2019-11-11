#include "pch.h"
#include "MSQLite3.h"
#include <stdexcept>

//------------------------ResultSet-----------------------------//

ResultSet::ResultSet()
{
	iterator = container.begin();
}

void ResultSet::addRecord(int count, const char** row, const char** cols)
{
	if (count)
	{
		Container::value_type icontain;
		for (int i = 0; i < count; i++)
			icontain.insert({ cols[i],(row[i] ? row[i] : "") });
		addRecord(std::move(icontain));
	}
}

void ResultSet::addRecord(Container::value_type&& record)
{
	container.emplace_back(std::move(record));
	iterator = container.begin();
}

ResultSet::operator bool()
{
	return container.size() > 0 && iterator != container.end();
}

bool ResultSet::next()
{
	if (iterator != container.end())
		iterator = std::next(iterator, 1);
	return iterator != container.end();
}

size_t ResultSet::count()
{
	return container.size();
}

//------------------------SQLite3-----------------------------//

SQLite3::SQLite3(const char* dbPath, const char* createStmt) :errMsg(nullptr), db(nullptr)
{
	result = sqlite3_open(dbPath, &db);
	isOpened = result == SQLITE_OK;

	//If statement to create database was provided
	if (createStmt)
		result = sqlite3_exec(db, createStmt, [](void* data, int count, char** row, char** columns)->int { return 0; }, 0, &errMsg);

	if (result != SQLITE_OK)
		throw SQLite3Error(std::string("Database can't be initialized: \nResult: ") + std::to_string(result) + "\t" + errMsg);
}

SQLite3::~SQLite3()
{
	if (errMsg != nullptr)
		sqlite3_free(errMsg);
	if (isOpened)
		sqlite3_close(db);
}

bool SQLite3::isPrepared() const
{
	return isOpened;
}

int SQLite3::prepare()
{
	return 0;
}

const char* SQLite3::error() const
{
	return errMsg ? errMsg : "NULL";
}

sqlite_int64 SQLite3::lastId() const
{
	return isPrepared() ? sqlite3_last_insert_rowid(db) : -1;
}

void SQLite3::execute(const char* sql)
{
	if (sql)
	{
		result = sqlite3_exec(db, sql, [](void* data, int count, char** row, char** columns)->int {return 0; }, 0, &errMsg);
		if (result != SQLITE_OK)
			throw SQLite3Error(errMsg);
	}
	else
		throw SQLite3Error("No sql parameter");
}

ResultSet SQLite3::executeQuery(const char* sql)
{
	if (sql)
	{
		ResultSet retval;
		result = sqlite3_exec(db, sql, [](void* data, int count, char** row, char** columns)->int
			{
				ResultSet* rs = (ResultSet*)data;
				rs->addRecord(count, (const char**)row, (const char**)columns);
				return 0;
			}, &retval, &errMsg);
		if (result != SQLITE_OK)
			throw SQLite3Error(errMsg);
		return retval;
	}
	else
		throw SQLite3Error("No sql parameter");
}

void SQLite3::beginTransaction()
{
	result = sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, &errMsg);

	if (result != SQLITE_OK)
		throw SQLite3Error(errMsg);
}

void SQLite3::endTransaction()
{
	result = sqlite3_exec(db, "END TRANSACTION", 0, 0, &errMsg); 
	
	if (result != SQLITE_OK)
		throw SQLite3Error(errMsg);
}

std::string SQLite3::toString(const std::tm& date)
{
	std::stringstream ss("");
	ss << std::put_time(&date, "%Y-%m-%d");
	return ss.str();
}

std::string SQLite3::toStringM(const std::tm& date)
{
	std::stringstream ss("");
	ss << std::put_time(&date, "%Y-%m");
	return ss.str();
}

PreparedStatement& PreparedStatement::reset()
{
	rc = sqlite3_clear_bindings(stmt);
	rc = sqlite3_reset(stmt);
	return *this;
}

PreparedStatement& PreparedStatement::execute()
{
	do {
		rc = sqlite3_step(stmt);
	} while (rc == SQLITE_ROW);

	if (rc != SQLITE_DONE)
		throw SQLite3Error(sqlite3_errmsg(db));

	return *this;
}

ResultSet PreparedStatement::executeQuery()
{
	ResultSet rs;
	while (sqlite3_step(stmt) == SQLITE_ROW) // While query has result-rows.
	{ 
		//  Iterate all columns and push column - name value pair to record
		ResultSet::Container::value_type record;
		for (int colIndex = 0; colIndex < sqlite3_column_count(stmt); ++colIndex) 
			record.insert({ sqlite3_column_name(stmt, colIndex), (const char*)sqlite3_column_text(stmt, colIndex) });

		rs.addRecord(std::move(record));
	}
	return rs;
}

PreparedStatement::PreparedStatement(PreparedStatement&& ps)
{
	*this = std::move(ps);
}

PreparedStatement& PreparedStatement::operator=(PreparedStatement&&ps)
{
	if (this->stmt)
		sqlite3_finalize(this->stmt);

	this->db = ps.db;
	this->rc = ps.rc;
	this->stmt = ps.stmt;
	this->paramCount = ps.paramCount;
	ps.db = nullptr;
	ps.stmt = nullptr;
	return *this;
}

PreparedStatement::~PreparedStatement()
{
	if(stmt)
		rc = sqlite3_finalize(stmt);
}
