/*
    Copyright 2008-2012, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include "fost-jsondb.hpp"
#include <fost/db>
#include <fost/unicode>
#include <fost/jsondb.hpp>

#include <fost/exception/not_null.hpp>
#include <fost/exception/out_of_range.hpp>
#include <fost/exception/transaction_fault.hpp>
#include <fost/exception/unexpected_eof.hpp>


using namespace fostlib;
namespace bfs = boost::filesystem;


/*
    fostlib::jsondb
*/


namespace {
    void do_save( const json &j, const bfs::wpath &path ) {
        bfs::wpath tmp(path);
        tmp.replace_extension(L".tmp");
        utf::save_file( tmp, json::unparse( j, false ) );
        if ( bfs::exists( path ) )
            bfs::remove( path );
        bfs::rename( tmp, path );
    }
    json *construct( const bfs::wpath &filename, const nullable< json > &default_db ) {
        try {
            return new json( json::parse( utf::load_file( filename ) ) );
        } catch ( exceptions::unexpected_eof & ) {
            if ( default_db.isnull() )
                throw;
            do_save( default_db.value(), filename );
            return new json( default_db.value() );
        }
    }
}


fostlib::jsondb::jsondb()
: m_blob( new json ) {
}

fostlib::jsondb::jsondb( const bfs::wpath &filename, const nullable< json > &default_db )
: m_blob( boost::lambda::bind( construct, filename, default_db ) ), filename( filename ) {
}


/*
    fostlib::jsondb::local
*/


namespace {
    json dump( json &j, const jcursor &p ) {
        return json( j[ p ] );
    }

    void do_insert( json &db, const jcursor &k, const json &v ) {
        k.insert( db, v );
    }
    void do_push_back( json &db, const jcursor &k, const json &v ) {
        k.push_back( db, v );
    }
    void do_update( json &db, const jcursor &k, const json &v, const json &old ) {
        if ( db.has_key( k ) && db[ k ] == old )
            k( db ) = v;
        else if ( db.has_key( k ) && db[ k ] != old )
            throw exceptions::transaction_fault( L"The value being updated is not the value that was meant to be updated" );
        else
            throw exceptions::null( L"This key position is empty so cannot be updated" );
    }
    void do_remove( json &db, const jcursor &k, const json &old ) {
        if ( db.has_key( k ) && db[ k ] == old )
            k.del_key( db );
        else if ( db.has_key( k ) && db[ k ] != old )
            throw exceptions::transaction_fault( L"The value being deleted is not the value that was meant to be deleted" );
        else
            throw exceptions::null( L"This key position has already been deleted" );
    }

    json &do_commit( json &j, const jsondb::operations_type &ops ) {
        json db( j );
        try {
            for ( jsondb::operations_type::const_iterator op( ops.begin() ); op != ops.end(); ++op )
                (*op)( j );
        } catch ( ... ) {
            j = db;
            throw;
        }
        return j;
    }
}


fostlib::jsondb::local::local( jsondb &db, const jcursor &pos )
: m_db( db ), m_position( pos ) {
    refresh();
}

void fostlib::jsondb::local::refresh() {
    m_local = m_db.m_blob.synchronous< json >( boost::lambda::bind( dump, boost::lambda::_1, m_position ) );
}

jsondb::local &fostlib::jsondb::local::insert( const jcursor &position, const json &item ) {
    do_insert( m_local, position, item );
    m_operations.push_back( boost::lambda::bind( do_insert, boost::lambda::_1, position, item ) );
    return *this;
}

jsondb::local &fostlib::jsondb::local::push_back(
    const jcursor &position, const json &item
) {
    do_push_back( m_local, position, item );
    m_operations.push_back(boost::lambda::bind(
        do_push_back, boost::lambda::_1, position, item));
    return *this;
}

jsondb::local &fostlib::jsondb::local::update( const jcursor &position, const json &item ) {
    json oldvalue = m_local[ position ];
    position.replace( m_local, item );
    m_operations.push_back( boost::lambda::bind( do_update, boost::lambda::_1, position, item, oldvalue ) );
    return *this;
}

jsondb::local &fostlib::jsondb::local::remove( const jcursor &position ) {
    json oldvalue = m_local[ position ];
    position.del_key( m_local );
    m_operations.push_back( boost::lambda::bind( do_remove, boost::lambda::_1, position, oldvalue ) );
    return *this;
}

void fostlib::jsondb::local::commit() {
    if ( !m_db.filename().isnull() )
        m_operations.push_back( boost::lambda::bind( do_save, boost::lambda::_1, m_db.filename().value() ) );
    try {
        m_local = m_db.m_blob.synchronous< json >( boost::lambda::bind( do_commit, boost::lambda::_1, m_operations ) );
    } catch ( ... ) {
        rollback();
        throw;
    }
    m_operations.clear();
}

void fostlib::jsondb::local::rollback() {
    m_operations.clear();
    refresh();
}
