/*
    Copyright 2016, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include "patch.hpp"
#include <fost/datetime>


namespace {


    const struct now : protected beanbag::patch::transform {
        now()
        : transform("op:now") {
        }

        transform_fn operator () (const fostlib::json &js) const {
            return
                [pos = fostlib::coerce<fostlib::jcursor>(js["path"])]
                (fostlib::jsondb::local &trans) {
                    trans.set(pos, fostlib::timestamp::now());
                };
        }
    } c_now;


}

