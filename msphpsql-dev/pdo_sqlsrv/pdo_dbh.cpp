//---------------------------------------------------------------------------------------------------------------------------------
// file: pdo_dbh.cpp
//
// Contents: Implements the PDO object for PDO_SQLSRV
//
// Microsoft Drivers 5.3 for PHP for SQL Server
// Copyright(c) Microsoft Corporation
// All rights reserved.
// MIT License
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the ""Software""), 
//  to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
//  and / or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
//  IN THE SOFTWARE.
//---------------------------------------------------------------------------------------------------------------------------------

#include "php_pdo_sqlsrv.h"

#include <string>
#include <sstream>

typedef const zend_function_entry pdo_sqlsrv_function_entry;

// *** internal variables and constants ***

namespace {

const char LAST_INSERT_ID_QUERY[] = "SELECT @@IDENTITY;";
const size_t LAST_INSERT_ID_BUFF_LEN = 50;    // size of the buffer to hold the string value of the last inserted id, which may be an int, bigint, decimal(p,0) or numeric(p,0)
const char SEQUENCE_CURRENT_VALUE_QUERY[] = "SELECT CURRENT_VALUE FROM SYS.SEQUENCES WHERE NAME=%s";
const int LAST_INSERT_ID_QUERY_MAX_LEN = sizeof( SEQUENCE_CURRENT_VALUE_QUERY ) + SQL_MAX_SQLSERVERNAME + 2; // include the quotes

// List of PDO supported connection options.
namespace PDOConnOptionNames {

const char Server[] = "Server";
const char APP[] = "APP";
const char ApplicationIntent[] = "ApplicationIntent";
const char AttachDBFileName[] = "AttachDbFileName";
const char Authentication[] = "Authentication";
const char ColumnEncryption[] = "ColumnEncryption";
const char ConnectionPooling[] = "ConnectionPooling";
const char ConnectRetryCount[] = "ConnectRetryCount";
const char ConnectRetryInterval[] = "ConnectRetryInterval";
const char Database[] = "Database";
const char Driver[] = "Driver";
const char Encrypt[] = "Encrypt";
const char Failover_Partner[] = "Failover_Partner";
const char KeyStoreAuthentication[] = "KeyStoreAuthentication";
const char KeyStorePrincipalId[] = "KeyStorePrincipalId";
const char KeyStoreSecret[] = "KeyStoreSecret";
const char LoginTimeout[] = "LoginTimeout";
const char MARS_Option[] = "MultipleActiveResultSets";
const char MultiSubnetFailover[] = "MultiSubnetFailover";
const char QuotedId[] = "QuotedId";
const char TraceFile[] = "TraceFile";
const char TraceOn[] = "TraceOn";
const char TrustServerCertificate[] = "TrustServerCertificate";
const char TransactionIsolation[] = "TransactionIsolation";
const char TransparentNetworkIPResolution[] = "TransparentNetworkIPResolution";
const char WSID[] = "WSID";

}

enum PDO_CONN_OPTIONS {

    PDO_CONN_OPTION_SERVER = SQLSRV_CONN_OPTION_DRIVER_SPECIFIC,

};

enum PDO_STMT_OPTIONS {

    PDO_STMT_OPTION_ENCODING = SQLSRV_STMT_OPTION_DRIVER_SPECIFIC,
    PDO_STMT_OPTION_DIRECT_QUERY,
    PDO_STMT_OPTION_CURSOR_SCROLL_TYPE,
    PDO_STMT_OPTION_CLIENT_BUFFER_MAX_KB_SIZE,
    PDO_STMT_OPTION_EMULATE_PREPARES,
    PDO_STMT_OPTION_FETCHES_NUMERIC_TYPE,
};

// List of all the statement options supported by this driver.
const stmt_option PDO_STMT_OPTS[] = {
 
    { NULL, 0, SQLSRV_STMT_OPTION_QUERY_TIMEOUT, std::unique_ptr<stmt_option_query_timeout>( new stmt_option_query_timeout ) },
    { NULL, 0, SQLSRV_STMT_OPTION_SCROLLABLE, std::unique_ptr<stmt_option_pdo_scrollable>( new stmt_option_pdo_scrollable ) },
    { NULL, 0, PDO_STMT_OPTION_ENCODING, std::unique_ptr<stmt_option_encoding>( new stmt_option_encoding ) },
    { NULL, 0, PDO_STMT_OPTION_DIRECT_QUERY, std::unique_ptr<stmt_option_direct_query>( new stmt_option_direct_query ) },
    { NULL, 0, PDO_STMT_OPTION_CURSOR_SCROLL_TYPE, std::unique_ptr<stmt_option_cursor_scroll_type>( new stmt_option_cursor_scroll_type ) },
    { NULL, 0, PDO_STMT_OPTION_CLIENT_BUFFER_MAX_KB_SIZE, std::unique_ptr<stmt_option_buffered_query_limit>( new stmt_option_buffered_query_limit ) },
    { NULL, 0, PDO_STMT_OPTION_EMULATE_PREPARES, std::unique_ptr<stmt_option_emulate_prepares>( new stmt_option_emulate_prepares ) },
    { NULL, 0, PDO_STMT_OPTION_FETCHES_NUMERIC_TYPE, std::unique_ptr<stmt_option_fetch_numeric>( new stmt_option_fetch_numeric ) },

    { NULL, 0, SQLSRV_STMT_OPTION_INVALID, std::unique_ptr<stmt_option_functor>{} },
};

// boolean connection string
struct pdo_bool_conn_str_func 
{
    static void func( _In_ connection_option const* option, _Inout_ zval* value, sqlsrv_conn* /*conn*/, _Out_ std::string& conn_str TSRMLS_DC );
};

struct pdo_txn_isolation_conn_attr_func 
{
    static void func( connection_option const* /*option*/, _In_ zval* value_z, _Inout_ sqlsrv_conn* conn, std::string& /*conn_str*/ TSRMLS_DC );
};

struct pdo_int_conn_str_func {

    static void func( _In_ connection_option const* option, _In_ zval* value, sqlsrv_conn* /*conn*/, _Out_ std::string& conn_str TSRMLS_DC )
    {
        TSRMLS_C;
        SQLSRV_ASSERT( Z_TYPE_P( value ) == IS_STRING, "Wrong zval type for this keyword" ) 

        std::string val_str = Z_STRVAL_P( value );
        
        conn_str += option->odbc_name;
        conn_str += "={";
        conn_str += val_str;
        conn_str += "};";
    }
};

template <unsigned int Attr>
struct pdo_int_conn_attr_func {

    static void func( connection_option const* /*option*/, _In_ zval* value, _Inout_ sqlsrv_conn* conn, std::string& /*conn_str*/ TSRMLS_DC )
    {
        try {
        
            SQLSRV_ASSERT( Z_TYPE_P( value ) == IS_STRING, "pdo_int_conn_attr_func: Unexpected zval type." );
            
            size_t val = static_cast<size_t>( atoi( Z_STRVAL_P( value )) );
            core::SQLSetConnectAttr( conn, Attr, reinterpret_cast<SQLPOINTER>( val ), SQL_IS_UINTEGER TSRMLS_CC );
        }
        catch( core::CoreException& ) {
            throw;
        }
    }
};

template <unsigned int Attr>
struct pdo_bool_conn_attr_func {

    static void func( connection_option const* /*option*/, _Inout_ zval* value, _Inout_ sqlsrv_conn* conn, std::string& /*conn_str*/ TSRMLS_DC )
    {
         try {
        
            core::SQLSetConnectAttr( conn, Attr, reinterpret_cast<SQLPOINTER>( core_str_zval_is_true( value )), 
                                     SQL_IS_UINTEGER TSRMLS_CC );
        }
        catch( core::CoreException& ) {
            throw;
        }
    }
};

// statement options related functions
void add_stmt_option_key( _Inout_ sqlsrv_context& ctx, _In_ size_t key, _Inout_ HashTable* options_ht, 
                         _Inout_ zval** data TSRMLS_DC );
void validate_stmt_options( _Inout_ sqlsrv_context& ctx, _Inout_ zval* stmt_options, _Inout_ HashTable* pdo_stmt_options_ht TSRMLS_DC );

}       // namespace


// List of all connection options supported by this driver.
const connection_option PDO_CONN_OPTS[] = {
    { 
        PDOConnOptionNames::Server,
        sizeof( PDOConnOptionNames::Server ),
        PDO_CONN_OPTION_SERVER,
        NULL,
        0,
        CONN_ATTR_STRING,
        conn_str_append_func::func 
    },
    { 
        PDOConnOptionNames::APP,
        sizeof( PDOConnOptionNames::APP ),
        SQLSRV_CONN_OPTION_APP,
        ODBCConnOptions::APP,
        sizeof( ODBCConnOptions::APP ),
        CONN_ATTR_STRING,
        conn_str_append_func::func 
    },
    { 
        PDOConnOptionNames::ApplicationIntent,
        sizeof( PDOConnOptionNames::ApplicationIntent ),
        SQLSRV_CONN_OPTION_APPLICATION_INTENT,
        ODBCConnOptions::ApplicationIntent,
        sizeof( ODBCConnOptions::ApplicationIntent ),
        CONN_ATTR_STRING,
        conn_str_append_func::func 
    },
    { 
        PDOConnOptionNames::AttachDBFileName,
        sizeof( PDOConnOptionNames::AttachDBFileName ),
        SQLSRV_CONN_OPTION_ATTACHDBFILENAME,
        ODBCConnOptions::AttachDBFileName,
        sizeof( ODBCConnOptions::AttachDBFileName ),
        CONN_ATTR_STRING,
        conn_str_append_func::func 
    },
    {
        PDOConnOptionNames::Authentication,
        sizeof( PDOConnOptionNames::Authentication ),
        SQLSRV_CONN_OPTION_AUTHENTICATION,
        ODBCConnOptions::Authentication,
        sizeof( ODBCConnOptions::Authentication ),
        CONN_ATTR_STRING,
        conn_str_append_func::func
    },
    {
        PDOConnOptionNames::ConnectionPooling,
        sizeof( PDOConnOptionNames::ConnectionPooling ),
        SQLSRV_CONN_OPTION_CONN_POOLING,
        ODBCConnOptions::ConnectionPooling,
        sizeof( ODBCConnOptions::ConnectionPooling ),
        CONN_ATTR_BOOL,
        conn_null_func::func
    },
    {
        PDOConnOptionNames::Driver,
        sizeof(PDOConnOptionNames::Driver),
        SQLSRV_CONN_OPTION_DRIVER,
        ODBCConnOptions::Driver,
        sizeof(ODBCConnOptions::Driver),
        CONN_ATTR_STRING,
        driver_set_func::func
    },
    {
        PDOConnOptionNames::ColumnEncryption,
        sizeof(PDOConnOptionNames::ColumnEncryption),
        SQLSRV_CONN_OPTION_COLUMNENCRYPTION,
        ODBCConnOptions::ColumnEncryption,
        sizeof(ODBCConnOptions::ColumnEncryption),
        CONN_ATTR_STRING,
        column_encryption_set_func::func
    },
    {
        PDOConnOptionNames::ConnectRetryCount,
        sizeof( PDOConnOptionNames::ConnectRetryCount ),
        SQLSRV_CONN_OPTION_CONN_RETRY_COUNT,
        ODBCConnOptions::ConnectRetryCount,
        sizeof( ODBCConnOptions::ConnectRetryCount ),
        CONN_ATTR_INT,
        pdo_int_conn_str_func::func
    },
    {
        PDOConnOptionNames::ConnectRetryInterval,
        sizeof( PDOConnOptionNames::ConnectRetryInterval ),
        SQLSRV_CONN_OPTION_CONN_RETRY_INTERVAL,
        ODBCConnOptions::ConnectRetryInterval,
        sizeof( ODBCConnOptions::ConnectRetryInterval ),
        CONN_ATTR_INT,
        pdo_int_conn_str_func::func
    },
    {
        PDOConnOptionNames::Database,
        sizeof( PDOConnOptionNames::Database ),
        SQLSRV_CONN_OPTION_DATABASE,
        ODBCConnOptions::Database,
        sizeof( ODBCConnOptions::Database ),
        CONN_ATTR_STRING,
        conn_str_append_func::func
    },
    {
        PDOConnOptionNames::Encrypt,
        sizeof( PDOConnOptionNames::Encrypt ),
        SQLSRV_CONN_OPTION_ENCRYPT,
        ODBCConnOptions::Encrypt, 
        sizeof( ODBCConnOptions::Encrypt ),
        CONN_ATTR_BOOL,
        pdo_bool_conn_str_func::func
    },
    { 
        PDOConnOptionNames::Failover_Partner,
        sizeof( PDOConnOptionNames::Failover_Partner ),
        SQLSRV_CONN_OPTION_FAILOVER_PARTNER,
        ODBCConnOptions::Failover_Partner,
        sizeof( ODBCConnOptions::Failover_Partner ), 
        CONN_ATTR_STRING,
        conn_str_append_func::func
    },
    {
        PDOConnOptionNames::KeyStoreAuthentication,
        sizeof( PDOConnOptionNames::KeyStoreAuthentication ),
        SQLSRV_CONN_OPTION_KEYSTORE_AUTHENTICATION,
        ODBCConnOptions::KeyStoreAuthentication,
        sizeof( ODBCConnOptions::KeyStoreAuthentication ),
        CONN_ATTR_STRING,
        ce_akv_str_set_func::func 
    },
    {
        PDOConnOptionNames::KeyStorePrincipalId,
        sizeof( PDOConnOptionNames::KeyStorePrincipalId ),
        SQLSRV_CONN_OPTION_KEYSTORE_PRINCIPAL_ID,
        ODBCConnOptions::KeyStorePrincipalId,
        sizeof( ODBCConnOptions::KeyStorePrincipalId ),
        CONN_ATTR_STRING,
        ce_akv_str_set_func::func 
    },
    {
        PDOConnOptionNames::KeyStoreSecret,
        sizeof( PDOConnOptionNames::KeyStoreSecret ),
        SQLSRV_CONN_OPTION_KEYSTORE_SECRET,
        ODBCConnOptions::KeyStoreSecret,
        sizeof( ODBCConnOptions::KeyStoreSecret ),
        CONN_ATTR_STRING,
        ce_akv_str_set_func::func
    },
    {
        PDOConnOptionNames::LoginTimeout,
        sizeof( PDOConnOptionNames::LoginTimeout ),
        SQLSRV_CONN_OPTION_LOGIN_TIMEOUT,
        ODBCConnOptions::LoginTimeout,
        sizeof( ODBCConnOptions::LoginTimeout ),
        CONN_ATTR_INT,
        pdo_int_conn_attr_func<SQL_ATTR_LOGIN_TIMEOUT>::func 
    },
    {
        PDOConnOptionNames::MARS_Option,
        sizeof( PDOConnOptionNames::MARS_Option ),
        SQLSRV_CONN_OPTION_MARS,
        ODBCConnOptions::MARS_ODBC,
        sizeof( ODBCConnOptions::MARS_ODBC ),
        CONN_ATTR_BOOL,
        pdo_bool_conn_str_func::func
    },
    {
        PDOConnOptionNames::MultiSubnetFailover,
        sizeof( PDOConnOptionNames::MultiSubnetFailover ),
        SQLSRV_CONN_OPTION_MULTI_SUBNET_FAILOVER,
        ODBCConnOptions::MultiSubnetFailover,
        sizeof( ODBCConnOptions::MultiSubnetFailover ),
        CONN_ATTR_BOOL,
        pdo_bool_conn_str_func::func
    },
    {
        PDOConnOptionNames::QuotedId,
        sizeof( PDOConnOptionNames::QuotedId ),
        SQLSRV_CONN_OPTION_QUOTED_ID,
        ODBCConnOptions::QuotedId,
        sizeof( ODBCConnOptions::QuotedId ),
        CONN_ATTR_BOOL,
        pdo_bool_conn_str_func::func
    },
    {
        PDOConnOptionNames::TraceFile,
        sizeof( PDOConnOptionNames::TraceFile ),
        SQLSRV_CONN_OPTION_TRACE_FILE,
        ODBCConnOptions::TraceFile,
        sizeof( ODBCConnOptions::TraceFile ), 
        CONN_ATTR_STRING,
        str_conn_attr_func<SQL_ATTR_TRACEFILE>::func 
    },
    {
        PDOConnOptionNames::TraceOn,
        sizeof( PDOConnOptionNames::TraceOn ),
        SQLSRV_CONN_OPTION_TRACE_ON,
        ODBCConnOptions::TraceOn,
        sizeof( ODBCConnOptions::TraceOn ),
        CONN_ATTR_BOOL,
        pdo_bool_conn_attr_func<SQL_ATTR_TRACE>::func
    },
    {
        PDOConnOptionNames::TransactionIsolation,
        sizeof( PDOConnOptionNames::TransactionIsolation ),
        SQLSRV_CONN_OPTION_TRANS_ISOLATION,
        ODBCConnOptions::TransactionIsolation,
        sizeof( ODBCConnOptions::TransactionIsolation ),
        CONN_ATTR_INT,
        pdo_txn_isolation_conn_attr_func::func
    },
    {
        PDOConnOptionNames::TrustServerCertificate,
        sizeof( PDOConnOptionNames::TrustServerCertificate ),
        SQLSRV_CONN_OPTION_TRUST_SERVER_CERT,
        ODBCConnOptions::TrustServerCertificate,
        sizeof( ODBCConnOptions::TrustServerCertificate ),
        CONN_ATTR_BOOL,
        pdo_bool_conn_str_func::func
    },
    {
        PDOConnOptionNames::TransparentNetworkIPResolution,
        sizeof(PDOConnOptionNames::TransparentNetworkIPResolution),
        SQLSRV_CONN_OPTION_TRANSPARENT_NETWORK_IP_RESOLUTION,
        ODBCConnOptions::TransparentNetworkIPResolution,
        sizeof(ODBCConnOptions::TransparentNetworkIPResolution),
        CONN_ATTR_STRING,
        conn_str_append_func::func
    },
    {
        PDOConnOptionNames::WSID,
        sizeof( PDOConnOptionNames::WSID ),
        SQLSRV_CONN_OPTION_WSID,
        ODBCConnOptions::WSID,
        sizeof( ODBCConnOptions::WSID ),
        CONN_ATTR_STRING, 
        conn_str_append_func::func
    },
    { NULL, 0, SQLSRV_CONN_OPTION_INVALID, NULL, 0 , CONN_ATTR_INVALID, NULL },  //terminate the table
};


// close the connection
int pdo_sqlsrv_dbh_close( _Inout_ pdo_dbh_t *dbh TSRMLS_DC );

// execute queries
int pdo_sqlsrv_dbh_prepare( _Inout_ pdo_dbh_t *dbh, _In_reads_(sql_len) const char *sql,
                            _Inout_ size_t sql_len, _Inout_ pdo_stmt_t *stmt, _In_ zval *driver_options TSRMLS_DC );
zend_long pdo_sqlsrv_dbh_do( _Inout_ pdo_dbh_t *dbh, _In_reads_bytes_(sql_len) const char *sql, _In_ size_t sql_len TSRMLS_DC );

// transaction support functions
int pdo_sqlsrv_dbh_commit( _Inout_ pdo_dbh_t *dbh TSRMLS_DC );
int pdo_sqlsrv_dbh_begin( _Inout_ pdo_dbh_t *dbh TSRMLS_DC );
int pdo_sqlsrv_dbh_rollback( _Inout_ pdo_dbh_t *dbh TSRMLS_DC );

// attribute functions
int pdo_sqlsrv_dbh_set_attr( _Inout_ pdo_dbh_t *dbh, _In_ zend_long attr, _Inout_ zval *val TSRMLS_DC );
int pdo_sqlsrv_dbh_get_attr( _Inout_ pdo_dbh_t *dbh, _In_ zend_long attr, _Inout_ zval *return_value TSRMLS_DC );

// return more information
int pdo_sqlsrv_dbh_return_error( _In_ pdo_dbh_t *dbh, _In_opt_ pdo_stmt_t *stmt,
                                 _Out_ zval *info TSRMLS_DC);

// return the last id generated by an executed SQL statement
char * pdo_sqlsrv_dbh_last_id( _Inout_ pdo_dbh_t *dbh, _In_z_ const char *name, _Out_ size_t* len TSRMLS_DC );

// additional methods are supported in this function
pdo_sqlsrv_function_entry *pdo_sqlsrv_get_driver_methods( _Inout_ pdo_dbh_t *dbh, int kind TSRMLS_DC );

// quote a string, meaning put quotes around it and escape any quotes within it
int pdo_sqlsrv_dbh_quote( _Inout_ pdo_dbh_t* dbh, _In_reads_(unquotedlen) const char* unquoted, _In_ size_t unquotedlen, _Outptr_result_buffer_(*quotedlen) char **quoted, _Out_ size_t* quotedlen,
                          enum pdo_param_type paramtype TSRMLS_DC );

struct pdo_dbh_methods pdo_sqlsrv_dbh_methods = {

    pdo_sqlsrv_dbh_close,
    pdo_sqlsrv_dbh_prepare,
    pdo_sqlsrv_dbh_do,
    pdo_sqlsrv_dbh_quote,
    pdo_sqlsrv_dbh_begin,
    pdo_sqlsrv_dbh_commit,
    pdo_sqlsrv_dbh_rollback,
    pdo_sqlsrv_dbh_set_attr,
    pdo_sqlsrv_dbh_last_id,
    pdo_sqlsrv_dbh_return_error,
    pdo_sqlsrv_dbh_get_attr,
    NULL,                           // check liveness not implemented
    pdo_sqlsrv_get_driver_methods,
    NULL,                            // request shutdown not implemented
    NULL                             // in transaction not implemented
};


// log a function entry point
#ifndef _WIN32
#define PDO_LOG_DBH_ENTRY \
{ \
    pdo_sqlsrv_dbh* driver_dbh = reinterpret_cast<pdo_sqlsrv_dbh*>( dbh->driver_data ); \
    driver_dbh->set_func( __FUNCTION__ ); \
    int length = strlen( __FUNCTION__ ) + strlen( ": entering" ); \
    char func[length+1]; \
    strcpy_s( func, sizeof( __FUNCTION__ ), __FUNCTION__ ); \
    strcat_s( func, length+1, ": entering" ); \
    LOG( SEV_NOTICE, func ); \
}
#else
#define PDO_LOG_DBH_ENTRY \
{ \
    pdo_sqlsrv_dbh* driver_dbh = reinterpret_cast<pdo_sqlsrv_dbh*>( dbh->driver_data ); \
    driver_dbh->set_func( __FUNCTION__ ); \
    LOG( SEV_NOTICE, __FUNCTION__ ## ": entering" ); \
}
#endif

// constructor for the internal object for connections
pdo_sqlsrv_dbh::pdo_sqlsrv_dbh( _In_ SQLHANDLE h, _In_ error_callback e, _In_ void* driver TSRMLS_DC ) :
    sqlsrv_conn( h, e, driver, SQLSRV_ENCODING_UTF8 TSRMLS_CC ),
    stmts( NULL ),
    direct_query( false ),
    query_timeout( QUERY_TIMEOUT_INVALID ),
    client_buffer_max_size( PDO_SQLSRV_G( client_buffer_max_size )),
    fetch_numeric( false )
{
    if( client_buffer_max_size < 0 ) {
        client_buffer_max_size = sqlsrv_buffered_result_set::BUFFERED_QUERY_LIMIT_DEFAULT;
        LOG( SEV_WARNING, INI_PDO_SQLSRV_CLIENT_BUFFER_MAX_SIZE " set to a invalid value.  Resetting to default value." );
    }
}

// pdo_sqlsrv_db_handle_factory
// Maps to PDO::__construct. 
// Factory method called by the PDO driver manager to create a SQLSRV PDO connection.
// Does the following things:
//   1.Sets the error handling temporarily to PDO_ERRMODE_EXCEPTION.  
//     (If an error occurs in this function, the PDO specification mandates that 
//     an exception be thrown, regardless of the error mode setting.)
//   2. Processes the driver options.
//   3. Creates a core_conn object by calling core_sqlsrv_connect.
//   4. Restores the previous error mode on success.
// alloc_own_columns is set to 1 to tell the PDO driver manager that we manage memory
// Parameters:
// dbh - The PDO managed structure for the connection.
// driver_options - A HashTable (within the zval) of options to use when creating the connection.
// Return:
// 0 for failure, 1 for success.
int pdo_sqlsrv_db_handle_factory( _Inout_ pdo_dbh_t *dbh, _In_opt_ zval *driver_options TSRMLS_DC) 
{
    LOG( SEV_NOTICE, "pdo_sqlsrv_db_handle_factory: entering" );

    hash_auto_ptr pdo_conn_options_ht;
    pdo_error_mode prev_err_mode = dbh->error_mode;

    // must be done in all cases so that even a failed connection can query the
    // object for errors.
    dbh->methods = &pdo_sqlsrv_dbh_methods;
    dbh->driver_data = NULL;
    zval* temp_server_z = NULL;
    sqlsrv_malloc_auto_ptr<conn_string_parser> dsn_parser;
    zval server_z;
    ZVAL_UNDEF( &server_z );

    try {
 
    // no matter what the error mode, we want exceptions thrown if the connection fails
    // to happen (per the PDO spec)
    dbh->error_mode = PDO_ERRMODE_EXCEPTION;

    g_pdo_henv_cp->set_driver( dbh );
    g_pdo_henv_ncp->set_driver( dbh );

    CHECK_CUSTOM_ERROR( driver_options && Z_TYPE_P( driver_options ) != IS_ARRAY, *g_pdo_henv_cp, SQLSRV_ERROR_CONN_OPTS_WRONG_TYPE ) {
        throw core::CoreException();
    }
	// throws PDOException if the ATTR_PERSISTENT is in connection options
	CHECK_CUSTOM_ERROR( dbh->is_persistent, *g_pdo_henv_cp, PDO_SQLSRV_ERROR_UNSUPPORTED_DBH_ATTR ) {
		dbh->refcount--;
		throw pdo::PDOException();
	}
	
    // Initialize the options array to be passed to the core layer
    ALLOC_HASHTABLE( pdo_conn_options_ht );

    core::sqlsrv_zend_hash_init( *g_pdo_henv_cp, pdo_conn_options_ht, 10 /* # of buckets */, 
                                 ZVAL_INTERNAL_DTOR, 0 /*persistent*/ TSRMLS_CC );

    // Either of g_pdo_henv_cp or g_pdo_henv_ncp can be used to propogate the error.
    dsn_parser = new ( sqlsrv_malloc( sizeof( conn_string_parser ))) conn_string_parser( *g_pdo_henv_cp, dbh->data_source, 
                                                                                          static_cast<int>( dbh->data_source_len ), pdo_conn_options_ht );
    dsn_parser->parse_conn_string( TSRMLS_C );
    
    // Extract the server name
    temp_server_z = zend_hash_index_find( pdo_conn_options_ht, PDO_CONN_OPTION_SERVER );

    CHECK_CUSTOM_ERROR(( temp_server_z == NULL ), g_pdo_henv_cp, PDO_SQLSRV_ERROR_SERVER_NOT_SPECIFIED ) {
        
        throw pdo::PDOException();
    }

    server_z = *temp_server_z;

    // Add a reference to the option value since we are deleting it from the hashtable
    zval_add_ref( &server_z );
    zend_hash_index_del( pdo_conn_options_ht, PDO_CONN_OPTION_SERVER );

    sqlsrv_conn* conn = core_sqlsrv_connect( *g_pdo_henv_cp, *g_pdo_henv_ncp, core::allocate_conn<pdo_sqlsrv_dbh>, Z_STRVAL( server_z ), 
                                             dbh->username, dbh->password, pdo_conn_options_ht, pdo_sqlsrv_handle_dbh_error, 
                                             PDO_CONN_OPTS, dbh, "pdo_sqlsrv_db_handle_factory" TSRMLS_CC );

    // Free the string in server_z after being used
    zend_string_release( Z_STR( server_z ));
                                
    SQLSRV_ASSERT( conn != NULL, "Invalid connection returned.  Exception should have been thrown." );

    // set the driver_data and methods to complete creation of the PDO object
    dbh->driver_data = conn;
    dbh->error_mode = prev_err_mode;    // reset the error mode
    dbh->alloc_own_columns = 1;         // we do our own memory management for columns
    dbh->native_case = PDO_CASE_NATURAL;// SQL Server supports mixed case types

    }
    catch( core::CoreException& ) {
    
        if ( Z_TYPE( server_z ) == IS_STRING ) {
            zend_string_release( Z_STR( server_z ));
        }
        dbh->error_mode = prev_err_mode;    // reset the error mode
        g_pdo_henv_cp->last_error().reset();    // reset the last error; callee will check if last_error exist before freeing it and setting it to NULL
        
        return 0;
    }
    catch( ... ) {

        DIE( "pdo_sqlsrv_db_handle_factory: Unknown exception caught" );
    }

    return 1;
}

// pdo_sqlsrv_dbh_close
// Maps to PDO::__destruct.
// Called when a PDO object is to be destroyed.
// By the time this function is called, PDO has already made sure that 
// all statements are disposed and the PDO object is the last item destroyed. 
// Parameters:
// dbh - The PDO managed connection object.
// Return:
// Always returns 1 for success.
int pdo_sqlsrv_dbh_close( _Inout_ pdo_dbh_t *dbh TSRMLS_DC )
{
    LOG( SEV_NOTICE, "pdo_sqlsrv_dbh_close: entering" );

    // if the connection didn't complete properly, driver_data isn't initialized.
    if( dbh->driver_data == NULL ) {

        return 1;
    }

    PDO_RESET_DBH_ERROR;

    // call the core layer close
    core_sqlsrv_close( reinterpret_cast<sqlsrv_conn*>( dbh->driver_data ) TSRMLS_CC );
    dbh->driver_data = NULL;

    // always return success that the connection is closed
    return 1;
}

// pdo_sqlsrv_dbh_prepare
// Called by PDO::prepare and PDOStatement::__construct.
// Creates a statement and prepares it for execution by PDO
// Paramters:
// dbh - The PDO managed connection object.
// sql - SQL query to be prepared.
// sql_len - Length of the sql query
// stmt - The PDO managed statement object.
// driver_options - User provided list of statement options.
// Return:
// 0 for failure, 1 for success.
int pdo_sqlsrv_dbh_prepare( _Inout_ pdo_dbh_t *dbh, _In_reads_(sql_len) const char *sql,
                            _Inout_ size_t sql_len, _Inout_ pdo_stmt_t *stmt, _In_ zval *driver_options TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    hash_auto_ptr pdo_stmt_options_ht;
    sqlsrv_malloc_auto_ptr<char> sql_rewrite;
    size_t sql_rewrite_len = 0;
    sqlsrv_malloc_auto_ptr<pdo_sqlsrv_stmt> driver_stmt;
    hash_auto_ptr placeholders;
    sqlsrv_malloc_auto_ptr<sql_string_parser> sql_parser;

    pdo_sqlsrv_dbh* driver_dbh = reinterpret_cast<pdo_sqlsrv_dbh*>( dbh->driver_data );
    SQLSRV_ASSERT(( driver_dbh != NULL ), "pdo_sqlsrv_dbh_prepare: dbh->driver_data was null");

    try {

        // assign the methods for the statement object.  This is necessary even if the 
        // statement fails so the user can retrieve the error information.
        stmt->methods = &pdo_sqlsrv_stmt_methods;
        stmt->supports_placeholders = PDO_PLACEHOLDER_POSITIONAL;   // we support parameterized queries with ?, not names

        // Initialize the options array to be passed to the core layer
        ALLOC_HASHTABLE( pdo_stmt_options_ht );
        core::sqlsrv_zend_hash_init( *driver_dbh , pdo_stmt_options_ht, 3 /* # of buckets */, 
                                     ZVAL_PTR_DTOR, 0 /*persistent*/ TSRMLS_CC );
        
        // Either of g_pdo_henv_cp or g_pdo_henv_ncp can be used to propogate the error.
        validate_stmt_options( *driver_dbh, driver_options, pdo_stmt_options_ht TSRMLS_CC );

        driver_stmt = static_cast<pdo_sqlsrv_stmt*>( core_sqlsrv_create_stmt( driver_dbh, core::allocate_stmt<pdo_sqlsrv_stmt>,
                                                                              pdo_stmt_options_ht, PDO_STMT_OPTS, 
                                                                              pdo_sqlsrv_handle_stmt_error, stmt TSRMLS_CC ));

        // if the user didn't set anything in the prepare options, then set the buffer limit
        // to the value set on the connection.
        if( driver_stmt->buffered_query_limit== sqlsrv_buffered_result_set::BUFFERED_QUERY_LIMIT_INVALID ) {
            
             driver_stmt->buffered_query_limit = driver_dbh->client_buffer_max_size;
        }

        // if the user didn't set anything in the prepare options, then set the query timeout 
        // to the value set on the connection.
        if(( driver_stmt->query_timeout == QUERY_TIMEOUT_INVALID ) && ( driver_dbh->query_timeout != QUERY_TIMEOUT_INVALID )) {
            
            core_sqlsrv_set_query_timeout( driver_stmt, driver_dbh->query_timeout TSRMLS_CC );
        }

        // rewrite named parameters in the query to positional parameters if we aren't letting PDO do the
        // parameter substitution for us
        if( stmt->supports_placeholders != PDO_PLACEHOLDER_NONE ) {

            // rewrite the query to map named parameters to positional parameters.  We do this rather than use the ODBC named
            // parameters for consistency with the PDO MySQL and PDO ODBC drivers.
            int zr = pdo_parse_params( stmt, const_cast<char*>( sql ), sql_len, &sql_rewrite, &sql_rewrite_len TSRMLS_CC );

            CHECK_ZEND_ERROR( zr, driver_dbh, PDO_SQLSRV_ERROR_PARAM_PARSE) {
                throw core::CoreException();
            }
            // if parameter substitution happened, use that query instead of the original
            if( sql_rewrite != 0) {
                sql = sql_rewrite;
                sql_len = sql_rewrite_len;
            }
        }

        if( !driver_stmt->direct_query && stmt->supports_placeholders != PDO_PLACEHOLDER_NONE ) {
  
            core_sqlsrv_prepare( driver_stmt, sql, sql_len TSRMLS_CC );
        }
        else if( driver_stmt->direct_query ) {

            if( driver_stmt->direct_query_subst_string ) {
                // we use efree rather than sqlsrv_free since sqlsrv_free may wrap another allocation scheme
                // and we use estrdup below to allocate the new string, which uses emalloc
                efree( reinterpret_cast<void*>( const_cast<char*>( driver_stmt->direct_query_subst_string )));
            }
            driver_stmt->direct_query_subst_string = estrdup( sql );
            driver_stmt->direct_query_subst_string_len = sql_len;
        }
        // else if stmt->support_placeholders == PDO_PLACEHOLDER_NONE means that stmt->active_query_string will be
        // set to the substituted query
        if ( stmt->supports_placeholders == PDO_PLACEHOLDER_NONE ) {
			// parse placeholders in the sql query into the placeholders ht
            ALLOC_HASHTABLE( placeholders );
            core::sqlsrv_zend_hash_init( *driver_dbh, placeholders, 5, ZVAL_PTR_DTOR /* dtor */, 0 /* persistent */ TSRMLS_CC );
            sql_parser = new ( sqlsrv_malloc( sizeof( sql_string_parser ))) sql_string_parser( *driver_dbh, stmt->query_string,
                static_cast<int>(stmt->query_stringlen), placeholders );
            sql_parser->parse_sql_string( TSRMLS_C );
            driver_stmt->placeholders = placeholders;
            placeholders.transferred();
        }

        stmt->driver_data = driver_stmt;
        driver_stmt.transferred();                   
    }
    // everything is cleaned up by this point
    // catch everything so the exception doesn't spill into the calling PDO code
    catch( core::CoreException& ) {

        if( driver_stmt ) {

            driver_stmt->~pdo_sqlsrv_stmt();
        }

        // in the event that the statement caused an error that was copied to the connection, update the
        // connection with the error's SQLSTATE.
        if( driver_dbh->last_error() ) {

            strcpy_s( dbh->error_code, sizeof( dbh->error_code ), 
                      reinterpret_cast<const char*>( driver_dbh->last_error()->sqlstate ));
        }

        return 0;
    }

    // catch any errant exception and die
    catch(...) {

        DIE( "pdo_sqlsrv_dbh_prepare: Unknown exception caught." );
    }

    return 1;
}


// pdo_sqlsrv_dbh_do
// Maps to PDO::exec.
// Execute a SQL statement, such as an insert, update or delete, and return 
// the number of rows affected.
// Parameters:
// dbh - the PDO connection object, which contains the ODBC handle
// sql - the query to execute
// sql_len - length of sql query
// Return
// # of rows affected, -1 for an error.
zend_long pdo_sqlsrv_dbh_do( _Inout_ pdo_dbh_t *dbh, _In_reads_bytes_(sql_len) const char *sql, _In_ size_t sql_len TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    pdo_sqlsrv_dbh* driver_dbh = static_cast<pdo_sqlsrv_dbh*>( dbh->driver_data );

    sqlsrv_malloc_auto_ptr<sqlsrv_stmt> driver_stmt;
    SQLLEN rows = 0;

    // verify that the data type sizes are the same.  If we ever upgrade to 64 bit we don't want the wrong
    // thing to happen here.
    SQLSRV_STATIC_ASSERT( sizeof( rows ) == sizeof( SQLLEN ));

    try {
 
        SQLSRV_ASSERT( sql != NULL, "NULL or empty SQL string passed." );
        SQLSRV_ASSERT( driver_dbh != NULL, "pdo_sqlsrv_dbh_do: driver_data object was NULL.");

        // temp PDO statement used for error handling if something happens
        pdo_stmt_t temp_stmt;
        temp_stmt.dbh = dbh;
        // allocate a full driver statement to take advantage of the error handling
        driver_stmt = core_sqlsrv_create_stmt( driver_dbh, core::allocate_stmt<pdo_sqlsrv_stmt>, NULL /*options_ht*/, 
                          NULL /*valid_stmt_opts*/, pdo_sqlsrv_handle_stmt_error, &temp_stmt TSRMLS_CC );
        driver_stmt->set_func( __FUNCTION__ );

        SQLRETURN execReturn = core_sqlsrv_execute( driver_stmt TSRMLS_CC, sql, static_cast<int>( sql_len ) );

        // since the user can give us a compound statement, we return the row count for the last set, and since the row count
        // isn't guaranteed to be valid until all the results have been fetched, we fetch them all first.

        if ( execReturn != SQL_NO_DATA && core_sqlsrv_has_any_result( driver_stmt TSRMLS_CC )) {

            SQLRETURN r = SQL_SUCCESS;

            do {

                rows = core::SQLRowCount( driver_stmt TSRMLS_CC );

                r = core::SQLMoreResults( driver_stmt TSRMLS_CC );

            } while ( r != SQL_NO_DATA );
        }

        // returning -1 forces PDO to return false, which signals an error occurred.  SQLRowCount returns -1 for a number of cases
        // naturally, so we override that here with no rows returned.
        if( rows == -1 ) {
            rows = 0;
        }
    }
    catch( core::CoreException& ) {

        // copy any errors on the statement to the connection so that the user sees them, since the statement is released
        // before this method returns
        strcpy_s( dbh->error_code, sizeof( dbh->error_code ),
                  reinterpret_cast<const char*>( driver_stmt->last_error()->sqlstate ));
        driver_dbh->set_last_error( driver_stmt->last_error() );
        
        if( driver_stmt ) {
            driver_stmt->~sqlsrv_stmt();
        }
        
        return -1;
    }
    catch( ... ) {

        DIE( "pdo_sqlsrv_dbh_do: Unknown exception caught." );
    }

    if( driver_stmt ) {
        driver_stmt->~sqlsrv_stmt();
    }

    return rows;
}


// transaction support functions

// pdo_sqlsrv_dbh_begin
// Maps to PDO::beginTransaction.
// Begins a transaction. Turns off auto-commit mode. The pdo_dbh_t::in_txn 
// flag is maintained by PDO so we dont have to worry about it.
// Parameters:
// dbh - The PDO managed connection object.
// Return:
// 0 for failure and 1 for success.
int pdo_sqlsrv_dbh_begin( _Inout_ pdo_dbh_t *dbh TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    try {
     
        SQLSRV_ASSERT( dbh != NULL, "pdo_sqlsrv_dbh_begin: pdo_dbh_t object was null" );

        sqlsrv_conn* driver_conn = reinterpret_cast<sqlsrv_conn*>( dbh->driver_data );
            
        SQLSRV_ASSERT( driver_conn != NULL, "pdo_sqlsrv_dbh_begin: driver_data object was null" );

        DEBUG_SQLSRV_ASSERT( !dbh->in_txn, "pdo_sqlsrv_dbh_begin: Already in transaction" );

        core_sqlsrv_begin_transaction( driver_conn TSRMLS_CC );
    
        return 1;
    }
    catch( core::CoreException& ) {

        return 0;
    }
    catch( ... ) {
        
        DIE ("pdo_sqlsrv_dbh_begin: Uncaught exception occurred.");
    }
}



// pdo_sqlsrv_dbh_commit
// Maps to PDO::commit.
// Commits a transaction. Returns the connection to auto-commit mode.
// PDO throws error if PDO::commit is called on a connection that is not in an active
// transaction. The pdo_dbh_t::in_txn flag is maintained by PDO so we dont have 
// to worry about it here.
// Parameters:
// dbh - The PDO managed connection object.
// Return:
// 0 for failure and 1 for success.
int pdo_sqlsrv_dbh_commit( _Inout_ pdo_dbh_t *dbh TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    try {
        
        SQLSRV_ASSERT( dbh != NULL, "pdo_sqlsrv_dbh_commit: pdo_dbh_t object was null" );
        
        sqlsrv_conn* driver_conn = reinterpret_cast<sqlsrv_conn*>( dbh->driver_data );
            
        SQLSRV_ASSERT( driver_conn != NULL, "pdo_sqlsrv_dbh_commit: driver_data object was null" );
        
        DEBUG_SQLSRV_ASSERT( dbh->in_txn, "pdo_sqlsrv_dbh_commit: Not in transaction" );
        
        core_sqlsrv_commit( driver_conn TSRMLS_CC );
                
        return 1;
    }
    catch( core::CoreException& ) {

        return 0;
    }
    catch( ... ) {
        
        DIE ("pdo_sqlsrv_dbh_commit: Uncaught exception occurred.");
    }
}

// pdo_sqlsrv_dbh_rollback
// Maps to PDO::rollback.
// Rolls back a transaction. Returns the connection in auto-commit mode.
// PDO throws error if PDO::rollBack is called on a connection that is not in an active
// transaction. The pdo_dbh_t::in_txn flag is maintained by PDO so we dont have 
// to worry about it here.
// Parameters:
// dbh - The PDO managed connection object.
// Return:
// 0 for failure and 1 for success.
int pdo_sqlsrv_dbh_rollback( _Inout_ pdo_dbh_t *dbh TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    try {
        SQLSRV_ASSERT( dbh != NULL, "pdo_sqlsrv_dbh_rollback: pdo_dbh_t object was null" );
        
        sqlsrv_conn* driver_conn = reinterpret_cast<sqlsrv_conn*>( dbh->driver_data );
            
        SQLSRV_ASSERT( driver_conn != NULL, "pdo_sqlsrv_dbh_rollback: driver_data object was null" );
        
        DEBUG_SQLSRV_ASSERT( dbh->in_txn, "pdo_sqlsrv_dbh_rollback: Not in transaction" );
        
        core_sqlsrv_rollback( driver_conn TSRMLS_CC );
    
        return 1;
    }
    catch( core::CoreException& ) {

        return 0;
    }
    catch( ... ) {
        
        DIE ("pdo_sqlsrv_dbh_rollback: Uncaught exception occurred.");
    }
}

// pdo_sqlsrv_dbh_set_attr
// Maps to PDO::setAttribute. Sets an attribute on the PDO connection object.
// PDO driver manager calls this function directly after calling the factory
// method for PDO, for any attribute which is specified in the PDO constructor. 
// Parameters:
// dbh - The PDO connection object maintained by PDO.
// attr - The attribute to be set.
// val - The value of the attribute to be set.
// Return:
// 0 for failure, 1 for success.
int pdo_sqlsrv_dbh_set_attr( _Inout_ pdo_dbh_t *dbh, _In_ zend_long attr, _Inout_ zval *val TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    pdo_sqlsrv_dbh* driver_dbh = static_cast<pdo_sqlsrv_dbh*>( dbh->driver_data );
    SQLSRV_ASSERT( driver_dbh != NULL, "pdo_sqlsrv_dbh_set_attr: driver_data object was NULL.");

    try {

        switch( attr ) {

            case SQLSRV_ATTR_ENCODING:
            {
                zend_long attr_value;
                if( Z_TYPE_P( val ) != IS_LONG ) {
                    THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_INVALID_ENCODING );
                }
                attr_value = Z_LVAL_P( val );
                switch( attr_value ) {

                    case SQLSRV_ENCODING_DEFAULT:
                        // when default is applied to a connection, that means use UTF-8 encoding
                        driver_dbh->set_encoding( SQLSRV_ENCODING_UTF8 );
                        break;
                    case SQLSRV_ENCODING_SYSTEM:
                    case SQLSRV_ENCODING_UTF8:
                        driver_dbh->set_encoding( static_cast<SQLSRV_ENCODING>( attr_value ));
                        break;
                    default:
                        THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_INVALID_ENCODING );
                        break;
                }
            }
            break;

            case SQLSRV_ATTR_DIRECT_QUERY:
                driver_dbh->direct_query = ( zend_is_true( val ) ) ? true : false;
                break;

            case SQLSRV_ATTR_QUERY_TIMEOUT:
                if( Z_TYPE_P( val ) != IS_LONG || Z_LVAL_P( val ) < 0 ) {
                    convert_to_string( val );
                    THROW_PDO_ERROR( driver_dbh, SQLSRV_ERROR_INVALID_QUERY_TIMEOUT_VALUE, Z_STRVAL_P( val ));
                }
                driver_dbh->query_timeout = static_cast<long>( Z_LVAL_P( val ) );
                break;

            case SQLSRV_ATTR_CLIENT_BUFFER_MAX_KB_SIZE:
                if( Z_TYPE_P( val ) != IS_LONG || Z_LVAL_P( val ) <= 0 ) {
                    convert_to_string( val );
                    THROW_PDO_ERROR( driver_dbh, SQLSRV_ERROR_INVALID_BUFFER_LIMIT, Z_STRVAL_P( val ));
                }
                driver_dbh->client_buffer_max_size = Z_LVAL_P( val );
                break;

            case SQLSRV_ATTR_FETCHES_NUMERIC_TYPE:
                driver_dbh->fetch_numeric = (zend_is_true(val)) ? true : false;
                break;

            // Not supported
            case PDO_ATTR_FETCH_TABLE_NAMES: 
            case PDO_ATTR_FETCH_CATALOG_NAMES: 
            case PDO_ATTR_PREFETCH:
            case PDO_ATTR_MAX_COLUMN_LEN:
            case PDO_ATTR_CURSOR_NAME:  
            case PDO_ATTR_AUTOCOMMIT:
            case PDO_ATTR_PERSISTENT:
            case PDO_ATTR_TIMEOUT:
            {
                THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_UNSUPPORTED_DBH_ATTR );
            }

            // Read-only
            case PDO_ATTR_SERVER_VERSION:
            case PDO_ATTR_SERVER_INFO:  
            case PDO_ATTR_CLIENT_VERSION:
            case PDO_ATTR_DRIVER_NAME:
            case PDO_ATTR_CONNECTION_STATUS:    
            {
                THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_READ_ONLY_DBH_ATTR );
            }

            // Statement level only
            case PDO_ATTR_EMULATE_PREPARES:
            case PDO_ATTR_CURSOR:
            case SQLSRV_ATTR_CURSOR_SCROLL_TYPE:    
            {
                THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_STMT_LEVEL_ATTR );
            }

            default:
            {
                THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_INVALID_DBH_ATTR );
                break;
            }
        }
    }
    catch( pdo::PDOException& ) {

        return 0;
    }

    return 1;
}


// pdo_sqlsrv_dbh_get_attr
// Maps to PDO::getAttribute. Gets an attribute on the PDO connection object.
// Parameters:
// dbh - The PDO connection object maintained by PDO.
// attr - The attribute to get.
// return_value - zval in which to return the attribute value.
// Return:
// 0 for failure, 1 for success.
int pdo_sqlsrv_dbh_get_attr( _Inout_ pdo_dbh_t *dbh, _In_ zend_long attr, _Inout_ zval *return_value TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    pdo_sqlsrv_dbh* driver_dbh = static_cast<pdo_sqlsrv_dbh*>( dbh->driver_data );
    SQLSRV_ASSERT( driver_dbh != NULL, "pdo_sqlsrv_dbh_get_attr: driver_data object was NULL.");

    try {

        switch( attr ) {

            // Not supported
            case PDO_ATTR_FETCH_TABLE_NAMES: 
            case PDO_ATTR_FETCH_CATALOG_NAMES: 
            case PDO_ATTR_PREFETCH:
            case PDO_ATTR_MAX_COLUMN_LEN:
            case PDO_ATTR_CURSOR_NAME:
            case PDO_ATTR_AUTOCOMMIT:
            case PDO_ATTR_TIMEOUT:
            {
                // PDO does not throw "not supported" error message for these attributes. 
                THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_UNSUPPORTED_DBH_ATTR );
            }

             // Statement level only
            case PDO_ATTR_EMULATE_PREPARES:
            case PDO_ATTR_CURSOR:
            case SQLSRV_ATTR_CURSOR_SCROLL_TYPE:    
            {
                THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_STMT_LEVEL_ATTR );
            }
        
            case PDO_ATTR_STRINGIFY_FETCHES:
            {
                // For this attribute, if we dont set the return_value than PDO returns NULL.
                ZVAL_BOOL(return_value, ( dbh->stringify ? 1 : 0 ) );
                break;
            }

            case PDO_ATTR_SERVER_INFO:
            {
                core_sqlsrv_get_server_info( driver_dbh, return_value TSRMLS_CC );
                break;
            }
            
            case PDO_ATTR_SERVER_VERSION:
            {
                core_sqlsrv_get_server_version( driver_dbh, return_value TSRMLS_CC );
                break;
            }
            
            case PDO_ATTR_CLIENT_VERSION:
            {
                core_sqlsrv_get_client_info( driver_dbh, return_value TSRMLS_CC );

                //Add the PDO SQLSRV driver's file version
                //Declarations below eliminate compiler warnings about string constant to char* conversions
                const char* extver = "ExtensionVer";
                std::string filever = VER_FILEVERSION_STR;
                core::sqlsrv_add_assoc_string( *driver_dbh, return_value, extver, &filever[0], 1 /*duplicate*/
                                               TSRMLS_CC );
                break;
            }

            case SQLSRV_ATTR_ENCODING:
            { 
                ZVAL_LONG( return_value, driver_dbh->encoding() );
                break;
            }

            case SQLSRV_ATTR_QUERY_TIMEOUT:
            { 
                ZVAL_LONG( return_value, ( driver_dbh->query_timeout == QUERY_TIMEOUT_INVALID ? 0 : driver_dbh->query_timeout ));
                break;
            }

            case SQLSRV_ATTR_DIRECT_QUERY:
            {
                ZVAL_BOOL( return_value, driver_dbh->direct_query );
                break;
            }

            case SQLSRV_ATTR_CLIENT_BUFFER_MAX_KB_SIZE:
            { 
                ZVAL_LONG( return_value, driver_dbh->client_buffer_max_size );
                break;
            }

            case SQLSRV_ATTR_FETCHES_NUMERIC_TYPE:
            {
                ZVAL_BOOL( return_value, driver_dbh->fetch_numeric );
                break;
            }

            default: 
            {
                THROW_PDO_ERROR( driver_dbh, PDO_SQLSRV_ERROR_INVALID_DBH_ATTR );
                break;
            }
        }

        return 1;
    }
    catch( core::CoreException& ) {
        return 0;
    }
}

// Called by PDO::errorInfo and PDOStatement::errorInfo.
// Returns the error info.
// Parameters:
// dbh - The PDO managed connection object.
// stmt - The PDO managed statement object.
// info - zval in which to return the error info.
// Return:
// 0 for failure, 1 for success.
int pdo_sqlsrv_dbh_return_error( _In_ pdo_dbh_t *dbh, _In_opt_ pdo_stmt_t *stmt,
                                 _Out_ zval *info TSRMLS_DC)
{
    SQLSRV_ASSERT( dbh != NULL || stmt != NULL, "Either dbh or stmt must not be NULL to dereference the error." );

    sqlsrv_error* ctx_error = NULL;
    if( stmt ) {
        ctx_error = static_cast<sqlsrv_stmt*>( stmt->driver_data )->last_error();
    }
    else {
        ctx_error = static_cast<sqlsrv_conn*>( dbh->driver_data )->last_error();
    }
    
    pdo_sqlsrv_retrieve_context_error( ctx_error, info );

    return 1;
}

// pdo_sqlsrv_dbh_last_id
// Maps to PDO::lastInsertId.
// Returns the last id generated by an executed SQL statement
// Parameters:
// dbh  - The PDO managed connection object.
// name - Table name.
// len  - Length of the name.
// Return:
// Returns the last insert id as a string.
char * pdo_sqlsrv_dbh_last_id( _Inout_ pdo_dbh_t *dbh, _In_z_ const char *name, _Out_ size_t* len TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    // turn off any error handling for last_id
    pdo_error_mode prev_err_mode = dbh->error_mode;
    dbh->error_mode = PDO_ERRMODE_SILENT;

    sqlsrv_malloc_auto_ptr<sqlsrv_stmt> driver_stmt;

    pdo_sqlsrv_dbh* driver_dbh = static_cast<pdo_sqlsrv_dbh*>( dbh->driver_data );
    SQLSRV_ASSERT( driver_dbh != NULL, "pdo_sqlsrv_dbh_last_id: driver_data object was NULL." );

    sqlsrv_malloc_auto_ptr<char> id_str;
    id_str = reinterpret_cast<char*>( sqlsrv_malloc( LAST_INSERT_ID_BUFF_LEN ));

    try {

        char last_insert_id_query[ LAST_INSERT_ID_QUERY_MAX_LEN ] = {'\0'};
        if( name == NULL ) {
            strcpy_s( last_insert_id_query, sizeof( last_insert_id_query ), LAST_INSERT_ID_QUERY );
        }
        else {
            char* quoted_table = NULL;
            size_t quoted_len = 0;
            int quoted = pdo_sqlsrv_dbh_quote( dbh, name, strnlen_s( name ), &quoted_table, &quoted_len, PDO_PARAM_NULL TSRMLS_CC );
            SQLSRV_ASSERT( quoted, "PDO::lastInsertId failed to quote the table name.");
            snprintf( last_insert_id_query, LAST_INSERT_ID_QUERY_MAX_LEN, SEQUENCE_CURRENT_VALUE_QUERY, quoted_table );
            sqlsrv_free( quoted_table );
        }

        // temp PDO statement used for error handling if something happens
        pdo_stmt_t temp_stmt;
        temp_stmt.dbh = dbh;

        // allocate a full driver statement to take advantage of the error handling
        driver_stmt = core_sqlsrv_create_stmt( driver_dbh, core::allocate_stmt<pdo_sqlsrv_stmt>, NULL /*options_ht*/, NULL /*valid_stmt_opts*/, pdo_sqlsrv_handle_stmt_error, &temp_stmt TSRMLS_CC );
        driver_stmt->set_func( __FUNCTION__ );

        
        sqlsrv_malloc_auto_ptr<SQLWCHAR> wsql_string;
        unsigned int wsql_len;
        wsql_string = utf16_string_from_mbcs_string( SQLSRV_ENCODING_CHAR, reinterpret_cast<const char*>( last_insert_id_query ), sizeof(last_insert_id_query), &wsql_len );

        CHECK_CUSTOM_ERROR( wsql_string == 0, driver_stmt, SQLSRV_ERROR_QUERY_STRING_ENCODING_TRANSLATE, get_last_error_message() ) {
                throw core::CoreException();
        }

        // execute the last insert id query        
        core::SQLExecDirectW( driver_stmt, wsql_string TSRMLS_CC );

        core::SQLFetchScroll( driver_stmt, SQL_FETCH_NEXT, 0 TSRMLS_CC );
        SQLRETURN r = core::SQLGetData( driver_stmt, 1, SQL_C_CHAR, id_str, LAST_INSERT_ID_BUFF_LEN, 
                                        reinterpret_cast<SQLLEN*>( len ), false TSRMLS_CC );

        CHECK_CUSTOM_ERROR( (!SQL_SUCCEEDED( r ) || *len == SQL_NULL_DATA || *len == SQL_NO_TOTAL), driver_stmt,
                            PDO_SQLSRV_ERROR_LAST_INSERT_ID ) {
            throw core::CoreException();
        }

        driver_stmt->~sqlsrv_stmt();
    }
    catch( core::CoreException& ) {

        // copy any errors on the statement to the connection so that the user sees them, since the statement is released
        // before this method returns
        strcpy_s( dbh->error_code, sizeof( dbh->error_code ),
                  reinterpret_cast<const char*>( driver_stmt->last_error()->sqlstate ));
        driver_dbh->set_last_error( driver_stmt->last_error() );
        
        if( driver_stmt ) {
            driver_stmt->~sqlsrv_stmt();
        }

        strcpy_s( id_str.get(), 1, "" );
        *len = 0;
    }

    char* ret_id_str = id_str.get();
    id_str.transferred();

    // restore error handling to its previous mode
    dbh->error_mode = prev_err_mode;

    return ret_id_str;
}

// pdo_sqlsrv_dbh_quote
// Maps to PDO::quote. As the name says, this function quotes a string.
// Always returns a valid string unless memory allocation fails.
// Parameters:
// dbh          - The PDO managed connection object.
// unquoted     - The unquoted string to be quoted.
// unquoted_len - Length of the unquoted string.
// quoted       - Buffer for output string.
// quoted_len   - Length of the output string.
// Return:
// 0 for failure, 1 for success.
int pdo_sqlsrv_dbh_quote( _Inout_ pdo_dbh_t* dbh, _In_reads_(unquoted_len) const char* unquoted, _In_ size_t unquoted_len, _Outptr_result_buffer_(*quoted_len) char **quoted, _Out_ size_t* quoted_len,
                          enum pdo_param_type /*paramtype*/ TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    SQLSRV_ENCODING encoding = SQLSRV_ENCODING_CHAR;
	
    // get the current object in PHP; this distinguishes pdo_sqlsrv_dbh_quote being called from:
    // 1. PDO::quote() - object name is PDO
    // 2. PDOStatement::execute() - object name is PDOStatement
    zend_execute_data* execute_data = EG( current_execute_data );
    zval *object = getThis();

    // iterate through parents to find "PDOStatement"
    bool is_statement = false;
    if ( object ) {
        zend_class_entry* curr_class = ( Z_OBJ_P( object ))->ce;
        while ( curr_class != NULL ) {
            if ( strcmp( reinterpret_cast<const char*>( curr_class->name->val ), "PDOStatement" ) == 0 ) {
                is_statement = true;
                break;
            }
            curr_class = curr_class->parent;
        }
    }
    // only change the encoding if quote is called from the statement level (which should only be called when a statement
    // is prepared with emulate prepared on)
    if ( is_statement ) {
        pdo_stmt_t *stmt = Z_PDO_STMT_P( object );
        // set the encoding to be the encoding of the statement otherwise set to be the encoding of the dbh
        pdo_sqlsrv_stmt* driver_stmt = reinterpret_cast<pdo_sqlsrv_stmt*>( stmt->driver_data );
        if ( driver_stmt->encoding() != SQLSRV_ENCODING_INVALID ) {
            encoding = driver_stmt->encoding();
        }
        else {
            pdo_sqlsrv_dbh* driver_dbh = reinterpret_cast<pdo_sqlsrv_dbh*>( stmt->driver_data );
            encoding = driver_dbh->encoding();
        }
        // get the placeholder at the current position in driver_stmt->placeholders ht
        zval* placeholder = NULL;
        if (( placeholder = zend_hash_get_current_data( driver_stmt->placeholders )) != NULL && zend_hash_move_forward( driver_stmt->placeholders ) == SUCCESS && stmt->bound_params != NULL ) {
            pdo_bound_param_data* param = NULL;
            if ( Z_TYPE_P( placeholder ) == IS_STRING ) {
                param = reinterpret_cast<pdo_bound_param_data*>( zend_hash_find_ptr( stmt->bound_params, Z_STR_P( placeholder )));
            }
            else if ( Z_TYPE_P( placeholder ) == IS_LONG) {
                param = reinterpret_cast<pdo_bound_param_data*>( zend_hash_index_find_ptr( stmt->bound_params, Z_LVAL_P( placeholder )));
            }
            if ( NULL != param ) {
                SQLSRV_ENCODING param_encoding = static_cast<SQLSRV_ENCODING>( Z_LVAL( param->driver_params ));
                if ( param_encoding != SQLSRV_ENCODING_INVALID ) {
                    encoding = param_encoding;
                }
            }
        }
    }

    if ( encoding == SQLSRV_ENCODING_BINARY ) {
        // convert from char* to hex digits using os
        std::basic_ostringstream<char> os;
        for ( size_t index = 0; index < unquoted_len && unquoted[ index ] != '\0'; ++index ) {
            // if unquoted is < 0 or > 255, that means this is a non-ascii character. Translation from non-ascii to binary is not supported.
            // return an empty terminated string for now
            if (( int )unquoted[ index ] < 0 || ( int )unquoted[ index ] > 255) {
                *quoted_len = 0;
                *quoted = reinterpret_cast<char*>( sqlsrv_malloc( *quoted_len, sizeof( char ), 1 ));
                ( *quoted )[ 0 ] = '\0';
                return 1;
            }
            // when an int is < 16 and is appended to os, its hex representation which starts
            // with '0' does not get appended properly (the starting '0' does not get appended)
            // thus append '0' first
            if (( int )unquoted[index] < 16 ) {
                os << '0';
            }
           os << std::hex << ( int )unquoted[ index ];
        }
        std::basic_string<char> str_hex = os.str();
        // each character is represented by 2 digits of hex
        size_t unquoted_str_len = unquoted_len * 2; // length returned should not account for null terminator
        char* unquoted_str = reinterpret_cast<char*>( sqlsrv_malloc( unquoted_str_len, sizeof( char ), 1 )); // include space for null terminator
        strcpy_s( unquoted_str, unquoted_str_len + 1 /* include null terminator*/, str_hex.c_str() );
        // include length of '0x' in the binary string
        *quoted_len = unquoted_str_len + 2;
        *quoted = reinterpret_cast<char*>( sqlsrv_malloc( *quoted_len, sizeof( char ), 1 ));
        unsigned int out_current = 0;
        // insert '0x'
        ( *quoted )[ out_current++ ] = '0';
        ( *quoted )[ out_current++ ] = 'x';
        for ( size_t index = 0; index < unquoted_str_len && unquoted_str[ index ] != '\0'; ++index ) {
            ( *quoted )[ out_current++ ] = unquoted_str[ index ];
        }
        // null terminator
        ( *quoted )[ out_current ] = '\0';
        sqlsrv_free( unquoted_str );
        return 1;
    }
    else {
        // count the number of quotes needed
        unsigned int quotes_needed = 2;  // the initial start and end quotes of course
        // include the N proceeding the initial quote if encoding is UTF8
        if ( encoding == SQLSRV_ENCODING_UTF8 ) {
            quotes_needed = 3;
        }
        for ( size_t index = 0; index < unquoted_len; ++index ) {
            if ( unquoted[ index ] == '\'' ) {
                ++quotes_needed;
            }
        }

        *quoted_len = unquoted_len + quotes_needed;  // length returned to the caller should not account for null terminator.
        *quoted = reinterpret_cast<char*>( sqlsrv_malloc( *quoted_len, sizeof( char ), 1 )); // include space for null terminator. 
        unsigned int out_current = 0;

        // insert N if the encoding is UTF8
        if ( encoding == SQLSRV_ENCODING_UTF8 ) {
            ( *quoted )[ out_current++ ] = 'N';
        }
        // insert initial quote
        ( *quoted )[ out_current++ ] = '\'';

        for ( size_t index = 0; index < unquoted_len; ++index ) {
            if ( unquoted[ index ] == '\'' ) {
                ( *quoted )[ out_current++ ] = '\'';
                ( *quoted )[ out_current++ ] = '\'';
            }
            else {
                ( *quoted )[ out_current++ ] = unquoted[ index ];
            }
        }

        // trailing quote and null terminator
        ( *quoted )[ out_current++ ] = '\'';
        ( *quoted )[ out_current ] = '\0';

        return 1;
    }
}

// This method is not implemented by this driver.
pdo_sqlsrv_function_entry *pdo_sqlsrv_get_driver_methods( _Inout_ pdo_dbh_t *dbh, int kind TSRMLS_DC )
{
    PDO_RESET_DBH_ERROR;
    PDO_VALIDATE_CONN;
    PDO_LOG_DBH_ENTRY;

    sqlsrv_conn* driver_conn = reinterpret_cast<sqlsrv_conn*>( dbh->driver_data );
    SQLSRV_ASSERT( driver_conn != NULL, "pdo_sqlsrv_get_driver_methods: driver_data object was NULL." );
    CHECK_CUSTOM_ERROR( true, driver_conn, PDO_SQLSRV_ERROR_FUNCTION_NOT_IMPLEMENTED ) {
        return NULL;
    }

    return NULL;    // to avoid a compiler warning
}

namespace {

// Maps the PDO driver specific statement option/attribute constants to the core layer 
// statement option/attribute constants.
void add_stmt_option_key( _Inout_ sqlsrv_context& ctx, _In_ size_t key, _Inout_ HashTable* options_ht, 
                         _Inout_ zval* data TSRMLS_DC )
{
     zend_ulong option_key = -1;
     switch( key ) {
  
         case PDO_ATTR_CURSOR:
             option_key = SQLSRV_STMT_OPTION_SCROLLABLE;
             break;
            
         case SQLSRV_ATTR_ENCODING:
             option_key = PDO_STMT_OPTION_ENCODING;
             break;

         case SQLSRV_ATTR_QUERY_TIMEOUT:
             option_key = SQLSRV_STMT_OPTION_QUERY_TIMEOUT;
             break;

         case PDO_ATTR_STATEMENT_CLASS:
             break;

         case SQLSRV_ATTR_DIRECT_QUERY:
             option_key = PDO_STMT_OPTION_DIRECT_QUERY;
             break;

         case SQLSRV_ATTR_CURSOR_SCROLL_TYPE:
             option_key = PDO_STMT_OPTION_CURSOR_SCROLL_TYPE;
             break;

         case SQLSRV_ATTR_CLIENT_BUFFER_MAX_KB_SIZE:
             option_key = PDO_STMT_OPTION_CLIENT_BUFFER_MAX_KB_SIZE;
             break;

         case PDO_ATTR_EMULATE_PREPARES:
             option_key = PDO_STMT_OPTION_EMULATE_PREPARES;
             break;

         case SQLSRV_ATTR_FETCHES_NUMERIC_TYPE:
             option_key = PDO_STMT_OPTION_FETCHES_NUMERIC_TYPE;
             break;

         default:
             CHECK_CUSTOM_ERROR( true, ctx, PDO_SQLSRV_ERROR_INVALID_STMT_OPTION ) {
                 throw core::CoreException();
             }
             break;
    }

    // if a PDO handled option makes it through (such as PDO_ATTR_STATEMENT_CLASS, just skip it
    if( option_key != -1 ) {
        zval_add_ref( data );
        core::sqlsrv_zend_hash_index_update(ctx, options_ht, option_key, data TSRMLS_CC );
    }
}


// validate_stmt_options
// Iterates through the list of statement options provided by the user and validates them 
// against the list of statement options provided by this driver. After validation
// creates a Hashtable of statement options to be sent to the core layer for processing.
// Parameters:
// ctx - The current context.
// stmt_options - The user provided list of statement options.
// pdo_stmt_options_ht - Output hashtable of statement options. 
void validate_stmt_options( _Inout_ sqlsrv_context& ctx, _Inout_ zval* stmt_options, _Inout_ HashTable* pdo_stmt_options_ht TSRMLS_DC )
{
    try {
        
        if( stmt_options ) {
           
            HashTable* options_ht = Z_ARRVAL_P( stmt_options );
            size_t int_key = -1;
            zend_string *key = NULL;
            zval* data = NULL;

            ZEND_HASH_FOREACH_KEY_VAL( options_ht, int_key, key, data ) {
                int type = HASH_KEY_NON_EXISTENT;
                int result = 0;
                type = key ? HASH_KEY_IS_STRING : HASH_KEY_IS_LONG;
                CHECK_CUSTOM_ERROR(( type != HASH_KEY_IS_LONG ), ctx, PDO_SQLSRV_ERROR_INVALID_STMT_OPTION ) {
                    throw core::CoreException();
                }

                add_stmt_option_key( ctx, int_key, pdo_stmt_options_ht, data TSRMLS_CC );
            } ZEND_HASH_FOREACH_END();
        }
    }
    catch( core::CoreException& ) {

        throw;
    }
}


void pdo_bool_conn_str_func::func( _In_ connection_option const* option, _Inout_ zval* value, sqlsrv_conn* /*conn*/, _Out_ std::string& conn_str TSRMLS_DC )
{
    TSRMLS_C;
    char const* val_str = "no";
   
    if( core_str_zval_is_true( value ) ) {
        
        val_str = "yes";
    }
     
    conn_str += option->odbc_name;
    conn_str += "={";
    conn_str += val_str;
    conn_str += "};";
}

void pdo_txn_isolation_conn_attr_func::func( connection_option const* /*option*/, _In_ zval* value_z, _Inout_ sqlsrv_conn* conn, 
                                             std::string& /*conn_str*/ TSRMLS_DC )
{
    try {
    
        SQLSRV_ASSERT( Z_TYPE_P( value_z ) == IS_STRING, "pdo_txn_isolation_conn_attr_func: Unexpected zval type." );
        const char* val = Z_STRVAL_P( value_z );
        size_t val_len = Z_STRLEN_P( value_z );
        zend_long out_val = SQL_TXN_READ_COMMITTED;

        // READ_COMMITTED
        if(( val_len == ( sizeof( PDOTxnIsolationValues::READ_COMMITTED ) - 1 ) 
             && !strcasecmp( val, PDOTxnIsolationValues::READ_COMMITTED ))) {
            
            out_val = SQL_TXN_READ_COMMITTED;    
        }

        // READ_UNCOMMITTED
        else if(( val_len == ( sizeof( PDOTxnIsolationValues::READ_UNCOMMITTED ) - 1 ) 
            && !strcasecmp( val, PDOTxnIsolationValues::READ_UNCOMMITTED ))) {
        
            out_val = SQL_TXN_READ_UNCOMMITTED;
        }

        // REPEATABLE_READ
        else if(( val_len == ( sizeof( PDOTxnIsolationValues::REPEATABLE_READ ) - 1 ) 
            && !strcasecmp( val, PDOTxnIsolationValues::REPEATABLE_READ ))) {
        
            out_val = SQL_TXN_REPEATABLE_READ;
        }
        
        // SERIALIZABLE
        else if(( val_len == ( sizeof( PDOTxnIsolationValues::SERIALIZABLE ) - 1 ) 
            && !strcasecmp( val, PDOTxnIsolationValues::SERIALIZABLE ))) {
        
            out_val = SQL_TXN_SERIALIZABLE;
        }

        // SNAPSHOT
        else if(( val_len == ( sizeof( PDOTxnIsolationValues::SNAPSHOT ) - 1 ) 
            && !strcasecmp( val, PDOTxnIsolationValues::SNAPSHOT ))) {
        
            out_val = SQL_TXN_SS_SNAPSHOT;
        }
        
        else {
         
            CHECK_CUSTOM_ERROR( true, conn, PDO_SQLSRV_ERROR_INVALID_DSN_VALUE, PDOConnOptionNames::TransactionIsolation ) {

                throw core::CoreException();
            }
        }
        
        core::SQLSetConnectAttr( conn, SQL_COPT_SS_TXN_ISOLATION, reinterpret_cast<SQLPOINTER>( out_val ), SQL_IS_UINTEGER TSRMLS_CC );

    }
    catch( core::CoreException& ) {

        throw;
    }
}

}       // namespace
