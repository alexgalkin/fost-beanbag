/*
    Copyright 2012 Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include <beanbag/raw.hpp>
#include <fost/exception/parse_error.hpp>
#include <fost/crypto>
#include <fost/log>
#include <fost/insert>
#include "databases.hpp"


namespace {
    const class beanbag::raw_view c_raw_beanbag("beanbag.raw");
}


beanbag::raw_view::raw_view(const fostlib::string &name)
: view(name) {
}

std::pair<boost::shared_ptr<fostlib::mime>, int> beanbag::raw_view::operator () (
    const fostlib::json &options, const fostlib::string &pathname,
    fostlib::http::server::request &req, const fostlib::host &host
) const {
    fostlib::json log;

    fostlib::mime::mime_headers response_headers;
    // We need to hold the shared_ptr for as long as we have the local
    // otherwise the database may get garbage collected whilst we're using
    // it
    boost::shared_ptr<fostlib::jsondb> db_ptr =
        beanbag::database(options["database"]);
    fostlib::jsondb::local db(*db_ptr);

    fostlib::split_type path = fostlib::split(pathname, "/");
    fostlib::json data_path; fostlib::jcursor position;
    for ( fostlib::split_type::const_iterator part(path.begin());
            part != path.end(); ++part ) {
        try {
            int index = fostlib::coerce<int>(*part);
            fostlib::push_back(data_path, index);
            position /= index;
        } catch ( fostlib::exceptions::parse_error& ) {
            fostlib::push_back(data_path, *part);
            position /= *part;
        }
    }
    fostlib::insert(log, "jcursor", data_path);

    std::pair<fostlib::json, int> data;
    if ( req.method() == "GET" )
        data = get(options, pathname, req, host, db, position);
    else if ( req.method() == "PUT" )
        data = put(options, pathname, req, host, db, position);
    else {
        boost::shared_ptr<fostlib::mime> response(
                new fostlib::text_body(
                    req.method() + " not supported",
                    response_headers, L"text/plain" ));
        return std::make_pair(response, 403);
    }
    fostlib::insert(log, "status-code", data.second);
    fostlib::string accept(
        req.data()->headers().exists("Accept") ?
            req.data()->headers()["Accept"].value() : "*/*");
    fostlib::insert(log, "accept", accept);
    fostlib::logging::debug(log);
    if ( data.second < 300 ) {
        if ( !req.query_string().isnull()
                || accept.find("application/json") < accept.find("text/html") )
            return std::make_pair(json_response(options,
                    data.first, response_headers, data_path, position), data.second);
        else
            return std::make_pair(html_response(options,
                    data.first, response_headers, data_path, position), data.second);
    } else {
        fostlib::string view_name = "fost.response." +
            fostlib::coerce<fostlib::string>(data.second);
        std::pair<fostlib::string, fostlib::json> view = find_view(view_name);
        return view_for(view.first)(view.second, pathname, req, host);
    }
}


fostlib::string beanbag::raw_view::etag(const fostlib::json &structure) const {
    fostlib::string json_string = fostlib::json::unparse(structure, false);
    return "\"" + fostlib::md5(json_string) + "\"";
}


std::pair<fostlib::json, int> beanbag::raw_view::get(
    const fostlib::json &, const fostlib::string &,
    fostlib::http::server::request &, const fostlib::host &,
    fostlib::jsondb::local &db, const fostlib::jcursor &position
) const {
    if ( position == fostlib::jcursor() || db.has_key(position) )
        return std::make_pair(db[position], 200);
    else
        return std::make_pair(fostlib::json(), 404);
}


std::pair<fostlib::json, int> beanbag::raw_view::put(
    const fostlib::json &options, const fostlib::string &pathname,
    fostlib::http::server::request &req, const fostlib::host &host,
    fostlib::jsondb::local &db, const fostlib::jcursor &position
) const {
    int status = 200;
    boost::shared_ptr< fostlib::binary_body > data(req.data());
    fostlib::string json_string = fostlib::coerce<fostlib::string>(
        data->data());
    fostlib::json new_data = fostlib::json::parse(json_string);
    if ( req.data()->headers().exists("If-Match") ) {
        fostlib::string ifmatch = req.data()->headers()["If-Match"].value();
        if ( ifmatch  == "*" ) {
            if ( !db.has_key(position) )
                status = 412;
        } else if ( ifmatch.find(etag(db[position])) == fostlib::string::npos )
            status = 412;
    }
    if ( status != 412 ) {
        if ( db.has_key(position) )
            db.update(position, new_data);
        else {
            status = 201;
            db.insert(position, new_data);
        }
        db.commit();
        return std::make_pair(db[position], status);
    }
    return std::make_pair(fostlib::json(), status);
}


boost::shared_ptr<fostlib::mime> beanbag::raw_view::json_response(
        const fostlib::json &options,
        const fostlib::json &body, fostlib::mime::mime_headers &headers,
        const fostlib::json &position_js, const fostlib::jcursor &position_jc) const {
    headers.set("ETag", etag(body));
    return boost::shared_ptr<fostlib::mime>( new fostlib::text_body(
        fostlib::json::unparse(body, true), headers, L"application/json" ) );
}


boost::shared_ptr<fostlib::mime> beanbag::raw_view::html_response(
        const fostlib::json &options,
        const fostlib::json &body, fostlib::mime::mime_headers &headers,
        const fostlib::json &position_js, const fostlib::jcursor &position_jc) const {
    fostlib::string html;
    if ( options["html"].has_key("redirect") ) {
        if ( position_jc == fostlib::jcursor() )
            html = fostlib::utf::load_file(
                fostlib::coerce<boost::filesystem::wpath>(
                    options["html"]["redirect"]["root"]));
    } else {
        html = fostlib::utf::load_file(
            fostlib::coerce<boost::filesystem::wpath>(options["html"]["template"]));

        html = replaceAll(html, "[[data]]",
            fostlib::json::unparse(body, true));
        html = replaceAll(html, "[[path]]",
            fostlib::json::unparse(position_js, false));
        html = replaceAll(html, "[[etag]]", etag(body));

        headers.set("ETag", "\"" + fostlib::md5(html) + "\"");
    }
    return boost::shared_ptr<fostlib::mime>(
            new fostlib::text_body(html,
                headers, L"text/html" ));
}
