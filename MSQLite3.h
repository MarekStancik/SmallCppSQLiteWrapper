#ifndef MSQLite3H
#define MSQLite3H
#include "sqlite3.h"
#include <ctime>
#include <unordered_map>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

//-------------Forward declarations-------------

//ResultSet class is Object that is beeing returned on executeQuery request
//This class is basically container that maps returned column names to its values
//To get value from ResultSet call get<> method and provide column name
//To read all returned rows iterate over ResultSet by method: next
//if there is no row left operator bool returns false
class ResultSet;

//PreparedStatement class is used to execute queries that needs prepared statements
//To create PreparedStatement you need to have SQLite3 class initialized and call function createPreparedStatement
//You insert the query for example like this : INSERT INTO table(col1,col2) VALUES(?,?) where ? is position of parameters.
//Then you need to bind values to that parameters so call preparedStatement.bind(value1,value2), 
//note that you have to bind same number of values as  is number of question marks, this bindable parameters can be also provided in constructor
//To execute command call execute() method
//To execute query which returns result set call executeQuery() method
//To reset the parameters but keep the query call reset() method - this can be used when you want to call same query multiple times with differen parameters
class PreparedStatement;

//SQLite3 class is the main connection to the database, this class handles the communication with database
//For better performance it is recommended to call beginTransaction() before and call endTransaction() after queries
//You can also execute raw queries by execute() and executeQuery() methods
class SQLite3;

//Error class for all SQLite3 Errors
class SQLite3Error;

class SQLite3Error : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

class ColumnNotFound : public SQLite3Error {
public:
	ColumnNotFound(const std::string& colName)
		:SQLite3Error("There is no column by name: '" + colName + "' in current resultset")
	{}
};

class ResultSet
{
public:
	//Container type to store and map returned rows/cols
	typedef std::vector<std::unordered_map<std::string, std::string>> Container;
private:
	//Internal container object
	Container container;

	//Iterator object for container
	Container::iterator iterator;
public:
	//Constructor - does nothing special
	ResultSet();

	ResultSet(sqlite3_stmt * stmt);

	//Add record to the result set
	void addRecord(int count, const char** row, const char** cols);
	void addRecord(Container::value_type&& record);
	
	//Returns true if container is still iterable
	operator bool();
	
	//Moves to the next row and returns false if it reached end
	bool next();

	//Return number of rows in resultset
	size_t count();

	//Return value for current row and given column name
	//Throws if no such column exist
	template<typename T>
	T get(const std::string& name)
	{
		T rval{0};
		auto it = iterator->find(name);
		if (it != iterator->end())
		{
			std::istringstream ss(it->second);
			ss >> rval;
		}
		else
			throw ColumnNotFound(name);
		return rval;
	}
};

//explicit specialization for std::string to take also a whitespace
template<>
inline std::string ResultSet::get(const std::string& name)
{
	std::string rval{};
	auto it = iterator->find(name);
	if (it != iterator->end())
	{
		std::istringstream ss(it->second);
		std::getline(ss, rval);
	}
	else
		throw ColumnNotFound(name);
	return rval;
}


//explicit specialization for std::tm to parse date correctly
template<>
inline std::tm ResultSet::get(const std::string& name)
{
	std::tm rval{};
	auto it = iterator->find(name);
	if (it != iterator->end())
	{
		std::stringstream ss(it->second);
		ss >> std::get_time(&rval, "%Y-%m-%d %H:%M:%S");
	}
	else
		throw ColumnNotFound(name);
	return rval;
}


class PreparedStatement
{
private:
	//Pointer to underlying db connection of statement
	//Memory management is handled in SQLite3 class
	sqlite3* db;

	//Pointer to statment object, acquired in constructor by sqlite3_prepare_v3 method
	//Freed by sqlite3_finalize method in destructor or during move operation
	sqlite3_stmt* stmt;

	//Result of sql operations
	int rc;

	//Count of parameters found in query
	int paramCount;

	//Prepare parameter of floating point type
	template<typename T>
	void prepareParam(const T& param, const int index
		,typename std::enable_if<std::is_floating_point<T>::value>::type* = 0){
		rc = sqlite3_bind_double(stmt, index, param);
	}

	//Prepare parameter of integral type
	template<typename T>
	void prepareParam(const T& param, const int index
		, typename std::enable_if<std::is_integral<T>::value>::type* = 0){
		rc = sqlite3_bind_int(stmt, index, param);
	}


	//Prepare parameter of int64 type
	void prepareParam(const sqlite_int64 param, const int index) {
		rc = sqlite3_bind_int64(stmt, index, param);
	}

	//Prepare parameter of const char* type
	void prepareParam(const char* param, const int index){
		rc = param ? sqlite3_bind_text(stmt, index, param, -1, SQLITE_STATIC)
            : sqlite3_bind_null(stmt,index);
	}

	//Prepare parameter of string type
	void prepareParam(const std::string& param, const int index) {
		rc = sqlite3_bind_text(stmt, index, param.c_str(), param.length(), SQLITE_STATIC);
	}

	//Prepare parameter of char type
	void prepareParam(const char& param, const int index) {
		rc = sqlite3_bind_text(stmt, index, &param, 1, SQLITE_STATIC);
	}

	//Prepare parameter of boolean type
	void prepareParam(const bool& param, const int index) {
		prepareParam(static_cast<int>(param), index);
	}


	//Constructor of object is in private section
	//It can be only called by SQLite3 component
	//Params: [db] underlying database pointer
	//[query] string query to prepare
	//[args] parameters for query
	template<typename ...Args>
	PreparedStatement(sqlite3* db, const std::string& query, Args&&... args)
		:
		rc(SQLITE_OK)
		,db(db)
		,stmt(nullptr)
		,paramCount(std::count(query.begin(), query.end(), '?'))
	{	
		//Prepares query and allocate stmt object
		rc = sqlite3_prepare_v3(db, query.c_str(),query.length(), 0, &stmt, 0);

		if (rc != SQLITE_OK)
			throw SQLite3Error(sqlite3_errmsg(db));
		
		//Bind only if there are some arguments
		if(sizeof...(args) > 0)
			bind(std::forward<Args>(args)...);
	}

	//So SQLite3 component can create PreparedStatement
	friend class SQLite3;
public:

	//Binds given arguments to sql query
	//Count of arguments have to be the same as the number of questionmarks in query
	template<typename ...Args>
	PreparedStatement& bind(Args&&... args) {
		//Check correct size
		if (sizeof...(args) != paramCount)
			throw SQLite3Error("Count of arguments does not equal count of questionmarks");

		//Prepare all parameters from parameter pack
		int index = 0;
		using expander = int[];
		(void)expander {
			0, (void(prepareParam(std::forward<Args>(args), ++index)), 0)...
		};

		if (rc != SQLITE_OK)
			throw SQLite3Error(sqlite3_errmsg(db));
		return *this;
	}

	//Resets the parameters but keeps the query
	//Is used to fill the same query with new params
	PreparedStatement& reset();

	//Executes statement
	PreparedStatement& execute();
	
	//Executes query
	ResultSet executeQuery();

	//Deleted because of stmt memory management
	PreparedStatement(const PreparedStatement&) = delete;
	PreparedStatement& operator=(const PreparedStatement&) = delete;

	//Only allowed move operations
	PreparedStatement(PreparedStatement&&);
	PreparedStatement& operator=(PreparedStatement&&);
	~PreparedStatement();
};

class SQLite3
{
private:
	//Db conneciton pointer
	sqlite3* db;

	//Error message pointer
	char* errMsg;

	//Results of sqlite3 calls
	int result;

	//flag to signalize if db is opened
	bool isOpened;
public:
	//Takes as parameter path to database and create statement
	SQLite3(const char* dbPath, const char* createStmt = nullptr);

	//closes and dealocates db
	~SQLite3();

	//return true if db is connected oterwise false
	bool isPrepared() const;

	//Tries to connect db 
	//not implemented
	int prepare();

	//returns last error message
	const char* error() const;

	//returns last inserted id
	sqlite_int64 lastId() const;

	//Following 2 operations are not recommended for security reasons
	//execute raw sql query
	void execute(const char* sql);

	//Executes raw sql query andreturn resultset of that query
	ResultSet executeQuery(const char* sql);

	//Creates prepared statement for this db connection
	template<typename ...Args>
	PreparedStatement createPreparedStatement(const std::string& query,Args&&... args){
		return PreparedStatement(db,query, std::forward<Args>(args)...);
	}

	void beginTransaction();
	void endTransaction();

	//converts date to date string in format %y-%m-%d
	static std::string toString(const std::tm& date);

	//converts date to date string in format %y-%m
	static std::string toStringM(const std::tm& date);
};

//Guard transaction
//Transaction starts at the inicialization of the object
//Transaction ends at destruction
//example
/*
	TransactionGuard tg(db);

	auto rs = db->createPreparedStatement("SELECT DISTINCT uniqueNumber FROM Receipt WHERE id = ?",id)
		.executeQuery();

	return rs ? rs.get<std::string>("uniqueNumber") : "";
*/
class TransactionGuard
{
private:
	SQLite3 * db;
public:
	TransactionGuard(SQLite3 * db):db(db){
		db->beginTransaction();
	}

	~TransactionGuard(){
		try{
			db->endTransaction();
		}catch(...){
        }
	}
};

#endif
