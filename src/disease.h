#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "catalua_type_operators.h"
#include "type_id.h"

class JsonObject;
class disease_type
{
    public:
        static void load_disease_type( const JsonObject &jo, const std::string &src );
        void load( const JsonObject &jo, const std::string & );
        static const std::vector<disease_type> &get_all();
        static void check_disease_consistency();
        static void reset();

        diseasetype_id id;
        bool was_loaded = false;
        time_duration min_duration = 1_turns;
        time_duration max_duration = 1_turns;
        int min_intensity = 1;
        int max_intensity = 1;
        /**Affected body parts*/
        std::set<body_part> affected_bodyparts;
        /**If not empty this sets the health threshold above which you're immune to the disease*/
        std::optional<int> health_threshold;
        /**effect applied by this disease*/
        efftype_id symptoms;

        LUA_TYPE_OPS( disease_type, id );

};

