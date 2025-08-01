// Associated headers here are the ones for which their only non-inline
// functions are serialization functions.  This allows IWYU to check the
// includes in such headers.
#include "enums.h" // IWYU pragma: associated
#include "npc_favor.h" // IWYU pragma: associated
#include "pldata.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "active_item_cache.h"
#include "activity_actor.h"
#include "assign.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_io.h"
#include "cata_variant.h"
#include "cata_utility.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "clone_ptr.h"
#include "clzones.h"
#include "computer.h"
#include "construction.h"
#include "consumption.h"
#include "craft_command.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "drop_token.h"
#include "effect.h"
#include "enum_conversions.h"
#include "event.h"
#include "faction.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_constants.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "kill_tracker.h"
#include "lru_cache.h"
#include "magic.h"
#include "magic_teleporter_list.h"
#include "map_memory.h"
#include "mapdata.h"
#include "mattack_common.h"
#include "mission.h"
#include "monster.h"
#include "morale.h"
#include "morale_types.h"
#include "mtype.h"
#include "npc.h"
#include "npc_class.h"
#include "options.h"
#include "overmapbuffer.h"
#include "pickup_token.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "profession.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "rng.h"
#include "scenario.h"
#include "skill.h"
#include "stats_tracker.h"
#include "stomach.h"
#include "string_id.h"
#include "submap.h"
#include "text_snippets.h"
#include "tileray.h"
#include "units.h"
#include "uistate.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "vpart_range.h"

struct mutation_branch;

static const efftype_id effect_riding( "riding" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_rad_badge( "rad_badge" );
static const itype_id itype_radio( "radio" );
static const itype_id itype_radio_on( "radio_on" );

static const std::array<std::string, NUM_OBJECTS> obj_type_name = { { "OBJECT_NONE", "OBJECT_ITEM", "OBJECT_ACTOR", "OBJECT_PLAYER",
        "OBJECT_NPC", "OBJECT_MONSTER", "OBJECT_VEHICLE", "OBJECT_TRAP", "OBJECT_FIELD",
        "OBJECT_TERRAIN", "OBJECT_FURNITURE"
    }
};

// TODO: investigate serializing other members of the Creature class hierarchy
static void serialize( const weak_ptr_fast<monster> &obj, JsonOut &jsout )
{
    if( const auto monster_ptr = obj.lock() ) {
        jsout.start_object();

        jsout.member( "monster_at", monster_ptr->pos() );
        // TODO: if monsters/Creatures ever get unique ids,
        // create a differently named member, e.g.
        //     jsout.member("unique_id", monster_ptr->getID());
        jsout.end_object();
    } else {
        // Monster went away. It's up the activity handler to detect this.
        jsout.write_null();
    }
}

static void deserialize( weak_ptr_fast<monster> &obj, JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    tripoint temp_pos;

    obj.reset();
    if( data.read( "monster_at", temp_pos ) ) {
        const auto monp = g->critter_tracker->find( temp_pos );

        if( monp == nullptr ) {
            debugmsg( "no monster found at %d,%d,%d", temp_pos.x, temp_pos.y, temp_pos.z );
            return;
        }

        obj = monp;
    }

    // TODO: if monsters/Creatures ever get unique ids,
    // look for a differently named member, e.g.
    //     data.read( "unique_id", unique_id );
    //     obj = g->id_registry->from_id( unique_id)
    //    }
}

void item_contents::serialize( JsonOut &json ) const
{
    if( !items.empty() ) {
        json.start_object();

        json.member( "items", items );

        json.end_object();
    }
}

void item_contents::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "items", items );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// player_activity.h

void player_activity::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", type );

    if( !type.is_null() ) {
        json.member( "actor", actor );
        json.member( "index", index );
        json.member( "position", position );
        json.member( "coords", coords );
        json.member( "coord_set", coord_set );
        json.member( "name", name );
        json.member( "targets", targets );
        json.member( "placement", placement );
        json.member( "values", values );
        json.member( "str_values", str_values );
        json.member( "auto_resume", auto_resume );
        json.member( "monsters", monsters );
        json.member( "tools", tools );
        json.member( "moves_total", moves_total );
        json.member( "moves_left", moves_left );
        json.member( "assistants_ids", assistants_ids_ );
    }
    json.end_object();
}

void player_activity::deserialize( JsonIn &jsin )
{
    static const activity_id ACT_MIGRATION_CANCEL( "ACT_MIGRATION_CANCEL" );
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "type", type );

    if( type.is_null() ) {
        return;
    }

    const bool has_actor = activity_actors::deserialize_functions.find( type ) !=
                           activity_actors::deserialize_functions.end();

    // Handle migration of pre-activity_actor activities
    // ACT_MIGRATION_CANCEL will clear the backlog and reset npc state
    // this may cause inconvenience but should avoid any lasting damage to npcs
    if( has_actor && type != ACT_MIGRATION_CANCEL ) {
        if( !data.has_member( "actor" ) ) {
            type = ACT_MIGRATION_CANCEL;
        } else {
            auto actor = data.get_object( "actor" );
            actor.allow_omitted_members();
            if( !actor.has_member( "actor_data" ) ) {
                type = ACT_MIGRATION_CANCEL;
            } else if( !actor.has_null( "actor_data" ) ) {
                auto a_data = actor.get_object( "actor_data" );
                a_data.allow_omitted_members();
                if( !a_data.has_member( "progress" ) ) {
                    type = ACT_MIGRATION_CANCEL;
                }
            }
        }
    } else {
        data.read( "moves_total", moves_total );
        int ml = data.get_int( "moves_left" );
        if( ml <= 0 ) {
            type = ACT_MIGRATION_CANCEL;
        } else {
            moves_left = ml;
        }
    }
    if( type != ACT_MIGRATION_CANCEL ) {
        data.read( "actor", actor );
    }
    data.read( "index", index );
    data.read( "position", position );
    data.read( "coords", coords );
    data.read( "coord_set", coord_set );
    data.read( "name", name );
    data.read( "targets", targets );
    data.read( "placement", placement );
    values = data.get_int_array( "values" );
    str_values = data.get_string_array( "str_values" );
    data.read( "auto_resume", auto_resume );
    data.read( "monsters", monsters );
    data.read( "tools", tools );
    data.read( "assistants_ids", assistants_ids_ );

}

void progress_counter::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "moves_total", moves_total );
    json.member( "moves_left", moves_left );
    json.member( "idx", idx );
    json.member( "total_tasks", total_tasks );
    json.member( "targets", targets );
    json.end_object();
}

void progress_counter::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "moves_total", moves_total );
    data.read( "moves_left", moves_left );
    data.read( "idx", idx );
    data.read( "total_tasks", total_tasks );
    auto arr = data.get_array( "targets" );
    for( JsonObject target : arr ) {
        targets.emplace_back( simple_task{
            .target_name = target.get_string( "target_name" ),
            .moves_total = target.get_int( "moves_total" ),
            .moves_left = target.get_int( "moves_left" ) } );
    }
}

void simple_task::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "target_name", target_name );
    json.member( "moves_total", moves_total );
    json.member( "moves_left", moves_left );
    json.end_object();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// requirements.h
void requirement_data::serialize( JsonOut &json ) const
{
    json.start_object();

    if( !is_null() ) {
        json.member( "blacklisted", blacklisted );
        const std::vector<std::vector<item_comp>> req_comps = get_components();
        const std::vector<std::vector<tool_comp>> tool_comps = get_tools();
        const std::vector<std::vector<quality_requirement>> quality_comps = get_qualities();

        json.member( "req_comps_total", req_comps );

        json.member( "tool_comps_total", tool_comps );

        json.member( "quality_comps_total", quality_comps );
    }
    json.end_object();
}

void requirement_data::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();

    data.read( "blacklisted", blacklisted );

    data.read( "req_comps_total", components );
    data.read( "tool_comps_total", tools );
    data.read( "quality_comps_total", qualities );

}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// skill.h
void SkillLevel::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "level", level() );
    json.member( "exercise", exercise( true ) );
    json.member( "istraining", isTraining() );
    json.member( "lastpracticed", _lastPracticed );
    json.member( "highestlevel", highestLevel() );
    json.end_object();
}

void SkillLevel::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "level", _level );
    data.read( "exercise", _exercise );
    data.read( "istraining", _isTraining );
    if( !data.read( "lastpracticed", _lastPracticed ) ) {
        _lastPracticed = calendar::start_of_cataclysm + time_duration::from_hours(
                             get_option<int>( "INITIAL_TIME" ) );
    }
    data.read( "highestlevel", _highestLevel );
    if( _highestLevel < _level ) {
        _highestLevel = _level;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// character_id.h

void character_id::serialize( JsonOut &jsout ) const
{
    jsout.write( value );
}

void character_id::deserialize( JsonIn &jsin )
{
    value = jsin.get_int();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// Character.h, avatar + npc

void char_trait_data::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "key", key );
    json.member( "charge", charge );
    json.member( "powered", powered );
    json.member( "show_sprite", show_sprite );
    json.end_object();
}

void char_trait_data::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "key", key );
    data.read( "charge", charge );
    data.read( "powered", powered );
    data.read( "show_sprite", show_sprite );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// consumption.h

void consumption_history_t::serialize( JsonOut &json ) const
{
    json.write( elems );
}

void consumption_history_t::deserialize( JsonIn &jsin )
{
    jsin.read( elems );
}

void consumption_event::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "time", time );
    json.member( "type_id", type_id );
    json.member( "component_hash", component_hash );
    json.end_object();
}

void consumption_event::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "time", time );
    jo.read( "type_id", type_id );
    jo.read( "component_hash", component_hash );
}

/**
 * Gather variables for saving. These variables are common to both the avatar and NPCs.
 */
void Character::load( const JsonObject &data )
{
    data.allow_omitted_members();
    Creature::load( data );

    if( !data.read( "posx", position.x ) ) {  // uh-oh.
        debugmsg( "BAD PLAYER/NPC JSON: no 'posx'?" );
    }
    data.read( "posy", position.y );
    if( !data.read( "posz", position.z ) && g != nullptr ) {
        position.z = g->get_levz();
    }
    // stats
    data.read( "str_cur", str_cur );
    data.read( "str_max", str_max );
    data.read( "dex_cur", dex_cur );
    data.read( "dex_max", dex_max );
    data.read( "int_cur", int_cur );
    data.read( "int_max", int_max );
    data.read( "per_cur", per_cur );
    data.read( "per_max", per_max );

    data.read( "str_bonus", str_bonus );
    data.read( "dex_bonus", dex_bonus );
    data.read( "per_bonus", per_bonus );
    data.read( "int_bonus", int_bonus );
    data.read( "omt_path", omt_path );

    std::string new_name;
    data.read( "name", new_name );
    if( !new_name.empty() ) {
        // Bugfix for name not having been saved properly
        name = new_name;
    }

    data.read( "base_age", init_age );
    data.read( "base_height", init_height );

    if( !data.read( "profession", prof ) || !prof.is_valid() ) {
        // We are likely an older profession which has since been removed so just set to default.
        // This is only cosmetic after game start.
        prof = profession::generic();
    }
    data.read( "custom_profession", custom_profession );

    // needs
    data.read( "thirst", thirst );
    data.read( "fatigue", fatigue );
    data.read( "sleep_deprivation", sleep_deprivation );
    data.read( "stored_calories", stored_calories );
    data.read( "radiation", radiation );
    data.read( "oxygen", oxygen );
    data.read( "pkill", pkill );

    data.read( "type_of_scent", type_of_scent );

    if( data.has_array( "ma_styles" ) ) {
        std::vector<matype_id> temp_styles;
        data.read( "ma_styles", temp_styles );
        bool temp_keep_hands_free = false;
        data.read( "keep_hands_free", temp_keep_hands_free );
        matype_id temp_selected_style;
        data.read( "style_selected", temp_selected_style );
        if( !temp_selected_style.is_valid() ) {
            temp_selected_style = matype_id( "style_none" );
        }
        martial_arts_data = pimpl<character_martial_arts>( character_martial_arts(
                                temp_styles, temp_selected_style, temp_keep_hands_free
                            ) );
    } else {
        data.read( "martial_arts_data", martial_arts_data );
    }

    JsonObject vits = data.get_object( "vitamin_levels" );
    vits.allow_omitted_members();
    for( const std::pair<const vitamin_id, vitamin> &v : vitamin::all() ) {
        if( vits.has_member( v.first.str() ) ) {
            int lvl = vits.get_int( v.first.str() );
            vitamin_levels[v.first] = clamp( lvl, v.first->min(), v.first->max() );
        }
    }
    data.read( "consumption_history", consumption_history );
    data.read( "activity", activity );
    data.read( "destination_activity", destination_activity );
    data.read( "stashed_outbounds_activity", stashed_outbounds_activity );
    data.read( "stashed_outbounds_backlog", stashed_outbounds_backlog );
    data.read( "backlog", backlog );
    if( !backlog.empty() && !backlog.front()->str_values.empty() && ( ( activity &&
            activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) || ( destination_activity &&
                    destination_activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) ) ) {
        requirement_data fetch_reqs;
        data.read( "fetch_data", fetch_reqs );
        const requirement_id req_id( backlog.front()->str_values.back() );
        requirement_data::save_requirement( fetch_reqs, req_id );
    }
    // npc activity on vehicles.
    data.read( "activity_vehicle_part_index", activity_vehicle_part_index );
    // health
    data.read( "healthy", healthy );
    data.read( "healthy_mod", healthy_mod );

    // @todo Remove after stable
    {
        std::array<int, num_bp> temp_cur_old, temp_conv_old, frostbite_timer_old;
        if( data.read( "temp_cur", temp_cur_old ) &&
            data.read( "temp_conv", temp_conv_old ) &&
            data.read( "frostbite_timer", frostbite_timer_old ) ) {
            // We can assume exactly num_bp body parts, since it's an old save
            for( size_t bp_iter = 0; bp_iter < num_bp; bp_iter++ ) {
                body_part bp_token = static_cast<body_part>( bp_iter );
                auto &part = get_part( convert_bp( bp_token ) );
                part.set_temp_cur( temp_cur_old[bp_iter] );
                part.set_temp_conv( temp_conv_old[bp_iter] );
                part.set_frostbite_timer( frostbite_timer_old[bp_iter] );
            }
        }
    }

    //energy
    data.read( "stim", stim );
    data.read( "stamina", stamina );

    data.read( "magic", magic );
    JsonArray parray;

    data.read( "traits", my_traits );
    for( auto it = my_traits.begin(); it != my_traits.end(); ) {
        const auto &tid = *it;
        if( tid.is_valid() ) {
            ++it;
        } else {
            debugmsg( "character %s has invalid trait %s, it will be ignored", name, tid.c_str() );
            my_traits.erase( it++ );
        }
    }

    data.read( "mutations", my_mutations );
    for( auto it = my_mutations.begin(); it != my_mutations.end(); ) {
        const trait_id &mid = it->first;
        if( mid.is_valid() ) {
            on_mutation_gain( mid );
            cached_mutations.push_back( &mid.obj() );
            ++it;
        } else {
            debugmsg( "character %s has invalid mutation %s, it will be ignored", name, mid.c_str() );
            it = my_mutations.erase( it );
        }
    }
    recalculate_size();

    data.read( "my_bionics", *my_bionics );

    for( auto &w : worn ) {
        w->on_takeoff( *this );
    }
    worn.clear();
    data.read( "worn", worn );
    for( auto &w : worn ) {
        on_item_wear( *w );
    }

    if( data.has_array( "hp_cur" ) ) {
        set_anatomy( anatomy_id( "human_anatomy" ) );
        set_body();
        std::array<int, 6> hp_cur;
        data.read( "hp_cur", hp_cur );
        std::array<int, 6> hp_max;
        data.read( "hp_max", hp_max );
        set_part_hp_max( bodypart_id( "head" ), hp_max[0] );
        set_part_hp_cur( bodypart_id( "head" ), hp_cur[0] );

        set_part_hp_max( bodypart_id( "torso" ), hp_max[1] );
        set_part_hp_cur( bodypart_id( "torso" ), hp_cur[1] );

        set_part_hp_max( bodypart_id( "arm_l" ), hp_max[2] );
        set_part_hp_cur( bodypart_id( "arm_l" ), hp_cur[2] );

        set_part_hp_max( bodypart_id( "arm_r" ), hp_max[3] );
        set_part_hp_cur( bodypart_id( "arm_r" ), hp_cur[3] );

        set_part_hp_max( bodypart_id( "leg_l" ), hp_max[4] );
        set_part_hp_cur( bodypart_id( "leg_l" ), hp_cur[4] );

        set_part_hp_max( bodypart_id( "leg_r" ), hp_max[5] );
        set_part_hp_cur( bodypart_id( "leg_r" ), hp_cur[5] );
    }


    inv.clear();
    if( data.has_member( "inv" ) ) {
        JsonIn *invin = data.get_raw( "inv" );
        inv.json_load_items( *invin );
    }

    remove_primary_weapon( );
    detached_ptr<item> weap;
    data.read( "weapon", weap );
    set_primary_weapon( std::move( weap ) );

    data.read( "move_mode", move_mode );

    if( has_effect( effect_riding ) ) {
        int temp_id;
        if( data.read( "mounted_creature", temp_id ) ) {
            mounted_creature_id = temp_id;
            mounted_creature = g->critter_tracker->from_temporary_id( temp_id );
        } else {
            mounted_creature = nullptr;
        }
    }

    morale->load( data );
    // Have to go through effects again, in case an effect gained a morale bonus
    for( const auto &elem : *effects ) {
        for( const std::pair<const bodypart_str_id, effect> &_effect_it : elem.second ) {
            const effect &e = _effect_it.second;
            on_effect_int_change( e.get_id(), e.get_intensity(), e.get_bp() );
        }
    }

    _skills->clear();
    for( const JsonMember member : data.get_object( "skills" ) ) {
        member.read( ( *_skills )[skill_id( member.name() )] );
    }

    data.read( "learned_recipes", *learned_recipes );
    autolearn_skills_stamp->clear(); // Invalidates the cache

    on_stat_change( "thirst", thirst );
    on_stat_change( "stored_calories", stored_calories );
    on_stat_change( "fatigue", fatigue );
    on_stat_change( "sleep_deprivation", sleep_deprivation );
    on_stat_change( "pkill", pkill );
    on_stat_change( "perceived_pain", get_perceived_pain() );
    recalc_sight_limits();
    reset_encumbrance();

    assign( data, "power_level", power_level, false, 0_kJ );
    assign( data, "max_power_level", max_power_level, false, 0_kJ );

    // Bionic power should not be negative!
    if( power_level < 0_J ) {
        power_level = 0_J;
    }

    JsonArray overmap_time_array = data.get_array( "overmap_time" );
    overmap_time.clear();
    while( overmap_time_array.has_more() ) {
        point_abs_omt pt;
        overmap_time_array.read_next( pt );
        time_duration tdr = 0_turns;
        overmap_time_array.read_next( tdr );
        overmap_time[pt] = tdr;
    }
    data.read( "stomach", stomach );
    data.read( "automoveroute", auto_move_route );

    known_traps.clear();
    for( JsonObject pmap : data.get_array( "known_traps" ) ) {
        pmap.allow_omitted_members();
        const tripoint p( pmap.get_int( "x" ), pmap.get_int( "y" ), pmap.get_int( "z" ) );
        const std::string t = pmap.get_string( "trap" );
        known_traps.insert( trap_map::value_type( p, t ) );
    }
}

/**
 * Load variables from json into object. These variables are common to both the avatar and NPCs.
 */
void Character::store( JsonOut &json ) const
{
    Creature::store( json );

    // assumes already in Character object
    // positional data
    json.member( "posx", position.x );
    json.member( "posy", position.y );
    json.member( "posz", position.z );

    // stat
    json.member( "str_cur", str_cur );
    json.member( "str_max", str_max );
    json.member( "dex_cur", dex_cur );
    json.member( "dex_max", dex_max );
    json.member( "int_cur", int_cur );
    json.member( "int_max", int_max );
    json.member( "per_cur", per_cur );
    json.member( "per_max", per_max );

    json.member( "str_bonus", str_bonus );
    json.member( "dex_bonus", dex_bonus );
    json.member( "per_bonus", per_bonus );
    json.member( "int_bonus", int_bonus );

    json.member( "name", name );

    json.member( "base_age", init_age );
    json.member( "base_height", init_height );

    if( prof.is_valid() ) {
        json.member( "profession", prof );
    }
    json.member( "custom_profession", custom_profession );

    // health
    json.member( "healthy", healthy );
    json.member( "healthy_mod", healthy_mod );

    // needs
    json.member( "thirst", thirst );
    json.member( "fatigue", fatigue );
    json.member( "sleep_deprivation", sleep_deprivation );
    json.member( "stored_calories", stored_calories );
    json.member( "radiation", radiation );
    json.member( "stamina", stamina );
    json.member( "vitamin_levels", vitamin_levels );
    json.member( "pkill", pkill );
    json.member( "omt_path", omt_path );
    json.member( "consumption_history", consumption_history );

    // crafting etc
    json.member( "destination_activity", destination_activity );
    json.member( "activity", activity );
    json.member( "stashed_outbounds_activity", stashed_outbounds_activity );
    json.member( "stashed_outbounds_backlog", stashed_outbounds_backlog );
    json.member( "backlog", backlog );
    json.member( "activity_vehicle_part_index", activity_vehicle_part_index ); // NPC activity

    // handling for storing activity requirements
    if( !backlog.empty() && !backlog.front()->str_values.empty() && ( ( activity &&
            activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) || ( destination_activity &&
                    destination_activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) ) ) {
        requirement_data things_to_fetch = requirement_id( backlog.front()->str_values.back() ).obj();
        json.member( "fetch_data", things_to_fetch );
    }

    const item &weapon = primary_weapon();
    if( !weapon.is_null() ) {
        json.member( "weapon", weapon ); // also saves contents
    }

    json.member( "stim", stim );
    json.member( "type_of_scent", type_of_scent );

    // breathing
    json.member( "oxygen", oxygen );

    // traits: permanent 'mutations' more or less
    json.member( "traits", my_traits );
    json.member( "mutations", my_mutations );
    json.member( "magic", magic );
    json.member( "martial_arts_data", martial_arts_data );
    // "Fracking Toasters" - Saul Tigh, toaster
    json.member( "my_bionics", *my_bionics );

    json.member_as_string( "move_mode",  move_mode );

    // storing the mount
    if( is_mounted() ) {
        json.member( "mounted_creature", g->critter_tracker->temporary_id( *mounted_creature ) );
    }

    morale->store( json );

    // skills
    json.member( "skills" );
    json.start_object();
    for( const auto &pair : *_skills ) {
        json.member( pair.first.str(), pair.second );
    }
    json.end_object();

    // npc: unimplemented, potentially useful
    json.member( "learned_recipes", *learned_recipes );
    autolearn_skills_stamp->clear(); // Invalidates the cache

    // npc; unimplemented
    if( power_level < 1_kJ ) {
        json.member( "power_level", std::to_string( units::to_joule( power_level ) ) + " J" );
    } else {
        json.member( "power_level", units::to_kilojoule( power_level ) );
    }
    json.member( "max_power_level", units::to_kilojoule( max_power_level ) );

    if( !overmap_time.empty() ) {
        json.member( "overmap_time" );
        json.start_array();
        for( const std::pair<const point_abs_omt, time_duration> &pr : overmap_time ) {
            json.write( pr.first );
            json.write( pr.second );
        }
        json.end_array();
    }
    json.member( "stomach", stomach );
    json.member( "automoveroute", auto_move_route );
    json.member( "known_traps" );
    json.start_array();
    for( const auto &elem : known_traps ) {
        json.start_object();
        json.member( "x", elem.first.x );
        json.member( "y", elem.first.y );
        json.member( "z", elem.first.z );
        json.member( "trap", elem.second );
        json.end_object();
    }
    json.end_array();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// player.h, avatar + npc

/**
 * Gather variables for saving. These variables are common to both the avatar and npcs.
 */
void player::store( JsonOut &json ) const
{
    Character::store( json );

    // energy
    json.member( "last_sleep_check", last_sleep_check );
    // misc levels
    json.member( "tank_plut", tank_plut );
    json.member( "reactor_plut", reactor_plut );
    json.member( "slow_rad", slow_rad );
    json.member( "scent", static_cast<int>( scent ) );

    // gender
    json.member( "male", male );

    json.member( "cash", cash );
    json.member( "recoil", recoil );
    json.member( "in_vehicle", in_vehicle );
    json.member( "id", getID() );

    // "Looks like I picked the wrong week to quit smoking." - Steve McCroskey
    json.member( "addictions", addictions );
    json.member( "followers", follower_ids );

    json.member( "worn", worn ); // also saves contents
    json.member( "inv" );
    inv.json_save_items( json );


    if( const auto lt_ptr = last_target.lock() ) {
        if( const npc *const guy = dynamic_cast<const npc *>( lt_ptr.get() ) ) {
            json.member( "last_target", guy->getID() );
            json.member( "last_target_type", +1 );
        } else if( const monster *const mon = dynamic_cast<const monster *>( lt_ptr.get() ) ) {
            // monsters don't have IDs, so get its index in the Creature_tracker instead
            json.member( "last_target", g->critter_tracker->temporary_id( *mon ) );
            json.member( "last_target_type", -1 );
        }
    } else {
        json.member( "last_target_pos", last_target_pos );
    }

    json.member( "destination_point", destination_point );
    json.member( "last_emote", last_emote );
    json.member( "ammo_location", ammo_location );
}

/**
 * Load variables from json into object. These variables are common to both the avatar and NPCs.
 */
void player::load( const JsonObject &data )
{
    Character::load( data );

    JsonArray parray;
    character_id tmpid;

    data.read( "tank_plut", tank_plut );
    data.read( "reactor_plut", reactor_plut );
    data.read( "slow_rad", slow_rad );
    data.read( "scent", scent );
    data.read( "male", male );
    data.read( "cash", cash );
    data.read( "recoil", recoil );
    data.read( "in_vehicle", in_vehicle );
    data.read( "last_sleep_check", last_sleep_check );
    if( data.read( "id", tmpid ) && tmpid.is_valid() ) {
        // Templates have invalid ids, so we only assign here when valid.
        // When the game starts, a new valid id will be assigned if not already
        // present.
        setID( tmpid );
    }

    data.read( "addictions", addictions );
    data.read( "followers", follower_ids );

    // Add the earplugs.
    if( has_bionic( bionic_id( "bio_ears" ) ) && !has_bionic( bionic_id( "bio_earplugs" ) ) ) {
        add_bionic( bionic_id( "bio_earplugs" ) );
    }

    // Add the blindfold.
    if( has_bionic( bionic_id( "bio_sunglasses" ) ) && !has_bionic( bionic_id( "bio_blindfold" ) ) ) {
        add_bionic( bionic_id( "bio_blindfold" ) );
    }

    // Fixes bugged characters for CBM's preventing mutations.
    for( const bionic &i : get_bionic_collection() ) {
        const bionic_id &bid = i.id;
        for( const trait_id &mid : bid->canceled_mutations ) {
            if( has_trait( mid ) ) {
                remove_mutation( mid );
            }
        }
    }

    int tmptar;
    int tmptartyp = 0;

    data.read( "last_target", tmptar );
    data.read( "last_target_type", tmptartyp );
    data.read( "last_target_pos", last_target_pos );
    data.read( "ammo_location", ammo_location );
    if( tmptartyp == +1 ) {
        // Use overmap_buffer because game::active_npc is not filled yet.
        last_target = overmap_buffer.find_npc( character_id( tmptar ) );
    } else if( tmptartyp == -1 ) {
        // Need to do this *after* the monsters have been loaded!
        last_target = g->critter_tracker->from_temporary_id( tmptar );
    }
    data.read( "destination_point", destination_point );
    data.read( "last_emote", last_emote );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// avatar.h

void avatar::serialize( JsonOut &json ) const
{
    json.start_object();

    store( json );

    json.end_object();
}

void avatar::store( JsonOut &json ) const
{
    player::store( json );

    if( g->scen != nullptr ) {
        json.member( "scenario", g->scen->ident() );
    }
    // someday, npcs may drive
    json.member( "controlling_vehicle", controlling_vehicle );

    // shopping carts, furniture etc
    json.member( "grab_point", grab_point );
    json.member( "grab_type", obj_type_name[static_cast<int>( grab_type ) ] );

    // misc player specific stuff
    json.member( "focus_pool", focus_pool );

    // stats through kills
    json.member( "str_upgrade", std::abs( str_upgrade ) );
    json.member( "dex_upgrade", std::abs( dex_upgrade ) );
    json.member( "int_upgrade", std::abs( int_upgrade ) );
    json.member( "per_upgrade", std::abs( per_upgrade ) );

    // Player only, books they have read at least once.
    json.member( "items_identified", items_identified );

    json.member( "translocators", translocators );

    // mission stuff
    json.member( "active_mission", active_mission == nullptr ? -1 : active_mission->get_id() );

    json.member( "active_missions", mission::to_uid_vector( active_missions ) );
    json.member( "completed_missions", mission::to_uid_vector( completed_missions ) );
    json.member( "failed_missions", mission::to_uid_vector( failed_missions ) );

    json.member( "show_map_memory", show_map_memory );

    json.member( "assigned_invlet" );
    json.start_array();
    for( auto iter : inv.assigned_invlet ) {
        json.start_array();
        json.write( iter.first );
        json.write( iter.second );
        json.end_array();
    }
    json.end_array();

    json.member( "invcache" );
    inv.json_save_invcache( json );

    json.member( "preferred_aiming_mode", preferred_aiming_mode );

    json.member( "snippets_read", snippets_read );

    json.member( "known_monsters", known_monsters );

    json.member( "faction_warnings" );
    json.start_array();
    for( const auto &elem : warning_record ) {
        json.start_object();
        json.member( "fac_warning_id", elem.first );
        json.member( "fac_warning_num", elem.second.first );
        json.member( "fac_warning_time", elem.second.second );
        json.end_object();
    }
    json.end_array();
}

void avatar::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    load( data );
}

void avatar::load( const JsonObject &data )
{
    player::load( data );

    data.read( "controlling_vehicle", controlling_vehicle );

    data.read( "grab_point", grab_point );
    std::string grab_typestr = "OBJECT_NONE";
    if( grab_point.x != 0 || grab_point.y != 0 ) {
        grab_typestr = "OBJECT_VEHICLE";
        data.read( "grab_type", grab_typestr );
    }
    const auto iter = std::find( obj_type_name.begin(), obj_type_name.end(), grab_typestr );
    grab( iter == obj_type_name.end() ?
          OBJECT_NONE : static_cast<object_type>( std::distance( obj_type_name.begin(), iter ) ),
          grab_point );

    data.read( "focus_pool", focus_pool );

    // stats through kills
    data.read( "str_upgrade", str_upgrade );
    data.read( "dex_upgrade", dex_upgrade );
    data.read( "int_upgrade", int_upgrade );
    data.read( "per_upgrade", per_upgrade );

    // this is so we don't need to call get_option in a draw function
    if( !get_option<bool>( "STATS_THROUGH_KILLS" ) )         {
        str_upgrade = -str_upgrade;
        dex_upgrade = -dex_upgrade;
        int_upgrade = -int_upgrade;
        per_upgrade = -per_upgrade;
    }

    data.read( "magic", magic );

    set_highest_cat_level();
    drench_mut_calc();
    std::string scen_ident = "(null)";
    if( data.read( "scenario", scen_ident ) && string_id<scenario>( scen_ident ).is_valid() ) {
        g->scen = &string_id<scenario>( scen_ident ).obj();

        if( !g->scen->allowed_start( start_location ) ) {
            start_location = g->scen->random_start_location();
        }
    } else {
        const scenario *generic_scenario = scenario::generic();
        // Only display error message if from a game file after scenarios existed.
        if( savegame_loading_version > 20 ) {
            debugmsg( "Tried to use non-existent scenario '%s'. Setting to generic '%s'.",
                      scen_ident.c_str(), generic_scenario->ident().c_str() );
        }
        g->scen = generic_scenario;
    }

    items_identified.clear();
    data.read( "items_identified", items_identified );

    // Player only, snippets they have read at least once.
    data.read( "snippets_read", snippets_read );

    data.read( "translocators", translocators );

    std::vector<int> tmpmissions;
    if( data.read( "active_missions", tmpmissions ) ) {
        active_missions = mission::to_ptr_vector( tmpmissions );
    }
    if( data.read( "failed_missions", tmpmissions ) ) {
        failed_missions = mission::to_ptr_vector( tmpmissions );
    }
    if( data.read( "completed_missions", tmpmissions ) ) {
        completed_missions = mission::to_ptr_vector( tmpmissions );
    }

    int tmpactive_mission = 0;
    if( data.read( "active_mission", tmpactive_mission ) ) {
        if( savegame_loading_version <= 23 ) {
            // In 0.C, active_mission was an index of the active_missions array (-1 indicated no active mission).
            // And it would as often as not be out of bounds (e.g. when a questgiver died).
            // Later, it became a mission * and stored as the mission's uid, and this change broke backward compatibility.
            // Unfortunately, nothing can be done about savegames between the bump to version 24 and 83808a941.
            if( tmpactive_mission >= 0 && tmpactive_mission < static_cast<int>( active_missions.size() ) ) {
                active_mission = active_missions[tmpactive_mission];
            } else if( !active_missions.empty() ) {
                active_mission = active_missions.back();
            }
        } else if( tmpactive_mission != -1 ) {
            active_mission = mission::find( tmpactive_mission );
        }
    }

    // Normally there is only one player character loaded, so if a mission that is assigned to
    // another character (not the current one) fails, the other character(s) are not informed.
    // We must inform them when they get loaded the next time.
    // Only active missions need checking, failed/complete will not change anymore.
    const auto last = std::remove_if( active_missions.begin(),
    active_missions.end(), []( mission const * m ) {
        return m->has_failed();
    } );
    std::copy( last, active_missions.end(), std::back_inserter( failed_missions ) );
    active_missions.erase( last, active_missions.end() );
    if( active_mission && active_mission->has_failed() ) {
        if( active_missions.empty() ) {
            active_mission = nullptr;
        } else {
            active_mission = active_missions.front();
        }
    }

    if( savegame_loading_version <= 23 && is_player() ) {
        // In 0.C there was no player_id member of mission, so it'll be the default -1.
        // When the member was introduced, no steps were taken to ensure compatibility with 0.C, so
        // missions will be buggy for saves between experimental commits bd2088c033 and dd83800.
        // see npc_chatbin::check_missions and npc::talk_to_u
        for( mission *miss : active_missions ) {
            miss->set_player_id_legacy_0c( getID() );
        }
    }

    data.read( "show_map_memory", show_map_memory );

    for( JsonArray pair : data.get_array( "assigned_invlet" ) ) {
        inv.assigned_invlet[static_cast<char>( pair.get_int( 0 ) )] =
            itype_id( pair.get_string( 1 ) );
    }

    if( data.has_member( "invcache" ) ) {
        JsonIn *jip = data.get_raw( "invcache" );
        inv.json_load_invcache( *jip );
    }

    data.read( "preferred_aiming_mode", preferred_aiming_mode );

    if( data.has_array( "faction_warnings" ) ) {
        for( JsonObject warning_data : data.get_array( "faction_warnings" ) ) {
            warning_data.allow_omitted_members();
            std::string fac_id = warning_data.get_string( "fac_warning_id" );
            int warning_num = warning_data.get_int( "fac_warning_num" );
            time_point warning_time = calendar::before_time_starts;
            warning_data.read( "fac_warning_time", warning_time );
            warning_record[faction_id( fac_id )] = std::make_pair( warning_num, warning_time );
        }
    }

    // monsters recorded by the character
    data.read( "known_monsters", known_monsters );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// npc.h

void npc_follower_rules::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "engagement", static_cast<int>( engagement ) );
    json.member( "aim", static_cast<int>( aim ) );
    json.member( "cbm_reserve", static_cast<int>( cbm_reserve ) );
    json.member( "cbm_recharge", static_cast<int>( cbm_recharge ) );

    // serialize the flags so they can be changed between save games
    for( const auto &rule : ally_rule_strs ) {
        json.member( "rule_" + rule.first, has_flag( rule.second.rule, false ) );
    }
    for( const auto &rule : ally_rule_strs ) {
        json.member( "override_enable_" + rule.first, has_override_enable( rule.second.rule ) );
    }
    for( const auto &rule : ally_rule_strs ) {
        json.member( "override_" + rule.first, has_override( rule.second.rule ) );
    }

    json.member( "pickup_whitelist", *pickup_whitelist );

    json.end_object();
}

void npc_follower_rules::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    int tmpeng = 0;
    data.read( "engagement", tmpeng );
    engagement = static_cast<combat_engagement>( tmpeng );
    int tmpaim = 0;
    data.read( "aim", tmpaim );
    aim = static_cast<aim_rule>( tmpaim );
    int tmpreserve = 50;
    data.read( "cbm_reserve", tmpreserve );
    cbm_reserve = static_cast<cbm_reserve_rule>( tmpreserve );
    int tmprecharge = 50;
    data.read( "cbm_recharge", tmprecharge );
    cbm_recharge = static_cast<cbm_recharge_rule>( tmprecharge );

    // deserialize the flags so they can be changed between save games
    for( const auto &rule : ally_rule_strs ) {
        bool tmpflag = false;
        // legacy to handle rules that were saved before overrides
        data.read( rule.first, tmpflag );
        if( tmpflag ) {
            set_flag( rule.second.rule );
        } else {
            clear_flag( rule.second.rule );
        }
        data.read( "rule_" + rule.first, tmpflag );
        if( tmpflag ) {
            set_flag( rule.second.rule );
        } else {
            clear_flag( rule.second.rule );
        }
        data.read( "override_enable_" + rule.first, tmpflag );
        if( tmpflag ) {
            enable_override( rule.second.rule );
        } else {
            disable_override( rule.second.rule );
        }
        data.read( "override_" + rule.first, tmpflag );
        if( tmpflag ) {
            set_override( rule.second.rule );
        } else {
            clear_override( rule.second.rule );
        }

        // This and the following two entries are for legacy save game handling.
        // "avoid_combat" was renamed "follow_close" to better reflect behavior.
        if( data.has_member( "rule_avoid_combat" ) ) {
            data.read( "rule_avoid_combat", tmpflag );
            if( tmpflag ) {
                set_flag( ally_rule::follow_close );
            } else {
                clear_flag( ally_rule::follow_close );
            }
        }
        if( data.has_member( "override_enable_avoid_combat" ) ) {
            data.read( "override_enable_avoid_combat", tmpflag );
            if( tmpflag ) {
                enable_override( ally_rule::follow_close );
            } else {
                disable_override( ally_rule::follow_close );
            }
        }
        if( data.has_member( "override_avoid_combat" ) ) {
            data.read( "override_avoid_combat", tmpflag );
            if( tmpflag ) {
                set_override( ally_rule::follow_close );
            } else {
                clear_override( ally_rule::follow_close );
            }
        }
    }

    data.read( "pickup_whitelist", *pickup_whitelist );
}

void npc_chatbin::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "first_topic", first_topic );
    if( mission_selected != nullptr ) {
        json.member( "mission_selected", mission_selected->get_id() );
    }
    json.member( "skill", skill );
    json.member( "style", style );
    json.member( "missions", mission::to_uid_vector( missions ) );
    json.member( "missions_assigned", mission::to_uid_vector( missions_assigned ) );
    json.end_object();
}

void npc_chatbin::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();

    if( data.has_int( "first_topic" ) ) {
        int tmptopic = 0;
        data.read( "first_topic", tmptopic );
        first_topic = convert_talk_topic( static_cast<talk_topic_enum>( tmptopic ) );
    } else {
        data.read( "first_topic", first_topic );
    }

    data.read( "skill", skill );
    data.read( "style", style );

    std::vector<int> tmpmissions;
    data.read( "missions", tmpmissions );
    missions = mission::to_ptr_vector( tmpmissions );
    std::vector<int> tmpmissions_assigned;
    data.read( "missions_assigned", tmpmissions_assigned );
    missions_assigned = mission::to_ptr_vector( tmpmissions_assigned );

    int tmpmission_selected = 0;
    mission_selected = nullptr;
    if( data.read( "mission_selected", tmpmission_selected ) && tmpmission_selected != -1 ) {
        if( savegame_loading_version <= 23 ) {
            // In 0.C, it was an index into the missions_assigned vector
            if( tmpmission_selected >= 0 &&
                tmpmission_selected < static_cast<int>( missions_assigned.size() ) ) {
                mission_selected = missions_assigned[tmpmission_selected];
            }
        } else {
            mission_selected = mission::find( tmpmission_selected );
        }
    }
}

void npc_personality::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    int tmpagg = 0;
    int tmpbrav = 0;
    int tmpcol = 0;
    int tmpalt = 0;
    if( data.read( "aggression", tmpagg ) &&
        data.read( "bravery", tmpbrav ) &&
        data.read( "collector", tmpcol ) &&
        data.read( "altruism", tmpalt ) ) {
        aggression = static_cast<signed char>( tmpagg );
        bravery = static_cast<signed char>( tmpbrav );
        collector = static_cast<signed char>( tmpcol );
        altruism = static_cast<signed char>( tmpalt );
    } else {
        debugmsg( "npc_personality: bad data" );
    }
}

void npc_personality::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "aggression", static_cast<int>( aggression ) );
    json.member( "bravery", static_cast<int>( bravery ) );
    json.member( "collector", static_cast<int>( collector ) );
    json.member( "altruism", static_cast<int>( altruism ) );
    json.end_object();
}

void npc_opinion::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "trust", trust );
    data.read( "fear", fear );
    data.read( "value", value );
    data.read( "anger", anger );
    data.read( "owed", owed );
}

void npc_opinion::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "trust", trust );
    json.member( "fear", fear );
    json.member( "value", value );
    json.member( "anger", anger );
    json.member( "owed", owed );
    json.end_object();
}

void npc_favor::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    type = static_cast<npc_favor_type>( jo.get_int( "type" ) );
    jo.read( "value", value );
    jo.read( "itype_id", item_id );
    if( jo.has_int( "skill_id" ) ) {
        skill = Skill::from_legacy_int( jo.get_int( "skill_id" ) );
    } else if( jo.has_string( "skill_id" ) ) {
        skill = skill_id( jo.get_string( "skill_id" ) );
    } else {
        skill = skill_id::NULL_ID();
    }
}

void npc_favor::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", static_cast<int>( type ) );
    json.member( "value", value );
    json.member( "itype_id", static_cast<std::string>( item_id ) );
    json.member( "skill_id", skill );
    json.end_object();
}

/*
 * load npc
 */
void npc::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    load( data );
}

void npc::load( const JsonObject &data )
{
    player::load( data );

    int misstmp = 0;
    int classtmp = 0;
    int atttmp = 0;
    std::string facID;
    std::string comp_miss_id;
    std::string comp_miss_role;
    tripoint_abs_omt comp_miss_pt;
    std::string classid;
    std::string companion_mission_role;
    time_point companion_mission_t = calendar::start_of_cataclysm;
    time_point companion_mission_t_r = calendar::start_of_cataclysm;
    std::string act_id;

    data.read( "marked_for_death", marked_for_death );
    data.read( "dead", dead );
    data.read( "patience", patience );
    if( data.has_number( "myclass" ) ) {
        data.read( "myclass", classtmp );
        myclass = npc_class::from_legacy_int( classtmp );
    } else if( data.has_string( "myclass" ) ) {
        data.read( "myclass", classid );
        myclass = npc_class_id( classid );
    }
    data.read( "known_to_u", known_to_u );
    data.read( "personality", personality );
    if( !data.read( "submap_coords", submap_coords ) ) {
        // Old submap coordinates are for the point (0, 0, 0) on local map
        // New ones are for submap that contains pos
        point old_coords;
        data.read( "mapx", old_coords.x );
        data.read( "mapy", old_coords.y );
        int o = 0;
        if( data.read( "omx", o ) ) {
            old_coords.x += o * OMAPX * 2;
        }
        if( data.read( "omy", o ) ) {
            old_coords.y += o * OMAPY * 2;
        }
        submap_coords = old_coords + point( posx() / SEEX, posy() / SEEY );
    }

    if( !data.read( "mapz", position.z ) ) {
        data.read( "omz", position.z ); // omz/mapz got moved to position.z
    }

    if( data.has_member( "plx" ) ) {
        last_player_seen_pos.emplace();
        data.read( "plx", last_player_seen_pos->x );
        data.read( "ply", last_player_seen_pos->y );
        if( !data.read( "plz", last_player_seen_pos->z ) ) {
            last_player_seen_pos->z = posz();
        }
        // old code used tripoint_min to indicate "not a valid point"
        if( *last_player_seen_pos == tripoint_min ) {
            last_player_seen_pos.reset();
        }
    } else {
        data.read( "last_player_seen_pos", last_player_seen_pos );
    }

    data.read( "goalx", goal.x() );
    data.read( "goaly", goal.y() );
    data.read( "goalz", goal.z() );

    data.read( "guardx", guard_pos.x );
    data.read( "guardy", guard_pos.y );
    data.read( "guardz", guard_pos.z );
    if( data.read( "current_activity_id", act_id ) ) {
        current_activity_id = activity_id( act_id );
    } else if( activity ) {
        current_activity_id = activity->id();
    }

    if( data.has_member( "pulp_locationx" ) ) {
        pulp_location.emplace();
        data.read( "pulp_locationx", pulp_location->x );
        data.read( "pulp_locationy", pulp_location->y );
        data.read( "pulp_locationz", pulp_location->z );
        // old code used tripoint_min to indicate "not a valid point"
        if( *pulp_location == tripoint_min ) {
            pulp_location.reset();
        }
    } else {
        data.read( "pulp_location", pulp_location );
    }
    data.read( "chair_pos", chair_pos );
    data.read( "wander_pos", wander_pos );
    if( data.read( "mission", misstmp ) ) {
        mission = static_cast<npc_mission>( misstmp );
        static const std::set<npc_mission> legacy_missions = {{
                NPC_MISSION_LEGACY_1, NPC_MISSION_LEGACY_2,
                NPC_MISSION_LEGACY_3
            }
        };
        if( legacy_missions.contains( mission ) ) {
            mission = NPC_MISSION_NULL;
        }
    }
    if( data.read( "previous_mission", misstmp ) ) {
        previous_mission = static_cast<npc_mission>( misstmp );
        static const std::set<npc_mission> legacy_missions = {{
                NPC_MISSION_LEGACY_1, NPC_MISSION_LEGACY_2,
                NPC_MISSION_LEGACY_3
            }
        };
        if( legacy_missions.contains( mission ) ) {
            previous_mission = NPC_MISSION_NULL;
        }
    }

    if( data.read( "my_fac", facID ) ) {
        fac_id = faction_id( facID );
    }
    int temp_fac_api_ver = 0;
    if( data.read( "faction_api_ver", temp_fac_api_ver ) ) {
        faction_api_version = temp_fac_api_ver;
    } else {
        faction_api_version = 0;
    }

    if( data.read( "attitude", atttmp ) ) {
        attitude = static_cast<npc_attitude>( atttmp );
        static const std::set<npc_attitude> legacy_attitudes = {{
                NPCATT_LEGACY_1, NPCATT_LEGACY_2, NPCATT_LEGACY_3,
                NPCATT_LEGACY_4, NPCATT_LEGACY_5, NPCATT_LEGACY_6
            }
        };
        if( legacy_attitudes.contains( attitude ) ) {
            attitude = NPCATT_NULL;
        }
    }
    if( data.read( "previous_attitude", atttmp ) ) {
        previous_attitude = static_cast<npc_attitude>( atttmp );
        static const std::set<npc_attitude> legacy_attitudes = {{
                NPCATT_LEGACY_1, NPCATT_LEGACY_2, NPCATT_LEGACY_3,
                NPCATT_LEGACY_4, NPCATT_LEGACY_5, NPCATT_LEGACY_6
            }
        };
        if( legacy_attitudes.contains( attitude ) ) {
            previous_attitude = NPCATT_NULL;
        }
    }

    if( data.read( "comp_mission_id", comp_miss_id ) ) {
        comp_mission.mission_id = comp_miss_id;
    }

    if( data.read( "comp_mission_pt", comp_miss_pt ) ) {
        comp_mission.position = comp_miss_pt;
    }

    if( data.read( "comp_mission_role", comp_miss_role ) ) {
        comp_mission.role_id = comp_miss_role;
    }

    if( data.read( "companion_mission_role_id", companion_mission_role ) ) {
        companion_mission_role_id = companion_mission_role;
    }

    std::vector<tripoint_abs_omt> companion_mission_pts;
    data.read( "companion_mission_points", companion_mission_pts );
    if( !companion_mission_pts.empty() ) {
        for( auto pt : companion_mission_pts ) {
            companion_mission_points.push_back( pt );
        }
    }

    if( !data.read( "companion_mission_time", companion_mission_t ) ) {
        companion_mission_time = calendar::before_time_starts;
    } else {
        companion_mission_time = companion_mission_t;
    }

    if( !data.read( "companion_mission_time_ret", companion_mission_t_r ) ) {
        companion_mission_time_ret = calendar::before_time_starts;
    } else {
        companion_mission_time_ret = companion_mission_t_r;
    }

    companion_mission_inv.clear();
    if( data.has_member( "companion_mission_inv" ) ) {
        JsonIn *invin_mission = data.get_raw( "companion_mission_inv" );
        companion_mission_inv.json_load_items( *invin_mission );
    }

    if( !data.read( "restock", restock ) ) {
        restock = calendar::before_time_starts;
    }

    data.read( "op_of_u", op_of_u );
    data.read( "chatbin", chatbin );
    if( !data.read( "rules", rules ) ) {
        data.read( "misc_rules", rules );
        data.read( "combat_rules", rules );
    }
    cbm_toggled = bionic_id::NULL_ID();
    data.read( "cbm_toggled", cbm_toggled );
    data.read( "cbm_fake_toggled", cbm_fake_toggled );
    if( cbm_toggled && !cbm_fake_toggled ) {
        cbm_fake_toggled = item::spawn( cbm_toggled->fake_item );
    }
    if( data.has_member( "cbm_weapon_index" ) ) {
        int index = 0;
        data.read( "cbm_weapon_index", index );
        if( index >= 0 ) {
            cbm_toggled = ( *my_bionics )[ index ].id;
            cbm_fake_toggled = item::spawn( cbm_toggled->fake_item );
        }
    }
    cbm_active = bionic_id::NULL_ID();
    data.read( "cbm_active", cbm_active );
    data.read( "cbm_fake_active", cbm_fake_active );

    if( !data.read( "last_updated", last_updated ) ) {
        last_updated = calendar::turn;
    }
    complaints.clear();
    data.read( "complaints", complaints );
}

/*
 * save npc
 */
void npc::serialize( JsonOut &json ) const
{
    json.start_object();
    // This must be after the json object has been started, so any super class
    // puts their data into the same json object.
    store( json );
    json.end_object();
}

void npc::store( JsonOut &json ) const
{
    player::store( json );

    json.member( "marked_for_death", marked_for_death );
    json.member( "dead", dead );
    json.member( "patience", patience );
    json.member( "myclass", myclass.str() );
    json.member( "known_to_u", known_to_u );
    json.member( "personality", personality );

    json.member( "submap_coords", submap_coords );

    json.member( "last_player_seen_pos", last_player_seen_pos );

    json.member( "goalx", goal.x() );
    json.member( "goaly", goal.y() );
    json.member( "goalz", goal.z() );

    json.member( "guardx", guard_pos.x );
    json.member( "guardy", guard_pos.y );
    json.member( "guardz", guard_pos.z );
    json.member( "current_activity_id", current_activity_id.str() );
    json.member( "pulp_location", pulp_location );
    json.member( "chair_pos", chair_pos );
    json.member( "wander_pos", wander_pos );
    // TODO: stringid
    json.member( "mission", mission );
    json.member( "previous_mission", previous_mission );
    json.member( "faction_api_ver", faction_api_version );
    if( !fac_id.str().empty() ) { // set in constructor
        json.member( "my_fac", fac_id.c_str() );
    }
    json.member( "attitude", static_cast<int>( attitude ) );
    json.member( "previous_attitude", static_cast<int>( previous_attitude ) );
    json.member( "op_of_u", op_of_u );
    json.member( "chatbin", chatbin );
    json.member( "rules", rules );

    json.member( "cbm_toggled", cbm_toggled );
    if( cbm_fake_toggled ) {
        json.member( "cbm_fake_toggled", cbm_fake_toggled );
    }
    json.member( "cbm_active", cbm_active );
    if( cbm_fake_active ) {
        json.member( "cbm_fake_active", cbm_fake_active );
    }

    json.member( "comp_mission_id", comp_mission.mission_id );
    json.member( "comp_mission_pt", comp_mission.position );
    json.member( "comp_mission_role", comp_mission.role_id );
    json.member( "companion_mission_role_id", companion_mission_role_id );
    json.member( "companion_mission_points", companion_mission_points );
    json.member( "companion_mission_time", companion_mission_time );
    json.member( "companion_mission_time_ret", companion_mission_time_ret );
    json.member( "companion_mission_inv" );
    companion_mission_inv.json_save_items( json );
    json.member( "restock", restock );

    json.member( "last_updated", last_updated );
    json.member( "complaints", complaints );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// inventory.h
/*
 * Save invlet cache
 */
void location_inventory::json_save_invcache( JsonOut &json ) const
{
    json.start_array();
    for( const auto &elem : inv.invlet_cache.get_invlets_by_id() ) {
        json.start_object();
        json.member( elem.first.str() );
        json.start_array();
        for( const auto &_sym : elem.second ) {
            json.write( static_cast<int>( _sym ) );
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();
}

/*
 * Invlet cache: player specific, thus not wrapped in inventory::json_load/save
 */
void location_inventory::json_load_invcache( JsonIn &jsin )
{
    try {
        std::unordered_map<itype_id, std::string> map;
        for( JsonObject jo : jsin.get_array() ) {
            jo.allow_omitted_members();
            for( const JsonMember member : jo ) {
                std::string invlets;
                for( const int i : member.get_array() ) {
                    invlets.push_back( i );
                }
                map[itype_id( member.name() )] = invlets;
            }
        }
        inv.invlet_cache = { map };
    } catch( const JsonError &jsonerr ) {
        debugmsg( "bad invcache json:\n%s", jsonerr.c_str() );
    }
}

/*
 * save all items. Just this->items, invlet cache saved separately
 */
void location_inventory::json_save_items( JsonOut &json ) const
{
    json.start_array();
    for( const auto &elem : inv.items ) {
        for( const auto &elem_stack_iter : elem ) {
            elem_stack_iter->serialize( json );
        }
    }
    json.end_array();
}

void location_inventory::json_load_items( JsonIn &jsin )
{
    jsin.start_array();
    while( !jsin.end_array() ) {
        add_item( item::spawn( jsin ), true, false );
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// monster.h

void monster::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    load( data );
}

void monster::load( const JsonObject &data )
{
    Creature::load( data );

    std::string sidtmp;
    // load->str->int
    data.read( "typeid", sidtmp );
    type = &mtype_id( sidtmp ).obj();

    data.read( "unique_name", unique_name );
    data.read( "posx", position.x );
    data.read( "posy", position.y );
    if( !data.read( "posz", position.z ) ) {
        position.z = g->get_levz();
    }

    data.read( "wandf", wandf );
    data.read( "wandx", wander_pos.x );
    data.read( "wandy", wander_pos.y );
    if( data.read( "wandz", wander_pos.z ) ) {
        wander_pos.z = position.z;
    }
    if( data.has_object( "tied_item" ) ) {
        JsonIn *tied_item_json = data.get_raw( "tied_item" );
        set_tied_item( item::spawn( *tied_item_json ) );
    }
    if( data.has_object( "tack_item" ) ) {
        JsonIn *tack_item_json = data.get_raw( "tack_item" );
        set_tack_item( item::spawn( *tack_item_json ) );
    }
    if( data.has_object( "armor_item" ) ) {
        JsonIn *armor_item_json = data.get_raw( "armor_item" );
        set_armor_item( item::spawn( *armor_item_json ) );
    }
    if( data.has_object( "storage_item" ) ) {
        JsonIn *storage_item_json = data.get_raw( "storage_item" );
        set_storage_item( item::spawn( *storage_item_json ) );
    }
    if( data.has_object( "battery_item" ) ) {
        JsonIn *battery_item_json = data.get_raw( "battery_item" );
        set_battery_item( item::spawn( *battery_item_json ) );
    }
    data.read( "hp", hp );

    // sp_timeout indicates an old save, prior to the special_attacks refactor
    if( data.has_array( "sp_timeout" ) ) {
        JsonArray parray = data.get_array( "sp_timeout" );
        size_t index = 0;
        int ptimeout = 0;
        while( parray.has_more() && index < type->special_attacks_names.size() ) {
            if( parray.read_next( ptimeout ) ) {
                // assume timeouts saved in same order as current monsters.json listing
                const std::string &aname = type->special_attacks_names[index++];
                auto &entry = special_attacks[aname];
                if( ptimeout >= 0 ) {
                    entry.cooldown = ptimeout;
                } else { // -1 means disabled, unclear what <-1 values mean in old saves
                    entry.cooldown = type->special_attacks.at( aname )->cooldown;
                    entry.enabled = false;
                }
            }
        }
    }

    // special_attacks indicates a save after the special_attacks refactor
    if( data.has_object( "special_attacks" ) ) {
        for( const JsonMember member : data.get_object( "special_attacks" ) ) {
            JsonObject saobject = member.get_object();
            saobject.allow_omitted_members();
            auto &entry = special_attacks[member.name()];
            entry.cooldown = saobject.get_int( "cooldown" );
            entry.enabled = saobject.get_bool( "enabled" );
        }
    }

    // make sure the loaded monster has every special attack its type says it should have
    for( auto &sa : type->special_attacks ) {
        const std::string &aname = sa.first;
        if( special_attacks.find( aname ) == special_attacks.end() ) {
            auto &entry = special_attacks[aname];
            entry.cooldown = rng( 0, sa.second->cooldown );
        }
    }

    data.read( "friendly", friendly );
    data.read( "mission_id", mission_id );
    data.read( "no_extra_death_drops", no_extra_death_drops );
    data.read( "dead", dead );
    data.read( "anger", anger );
    data.read( "morale", morale );
    data.read( "hallucination", hallucination );
    data.read( "aggro_character", aggro_character );
    data.read( "stairscount", staircount ); // really?
    data.read( "fish_population", fish_population );
    // Load legacy plans.
    std::vector<tripoint> plans;
    data.read( "plans", plans );
    if( !plans.empty() ) {
        goal = plans.back();
    }

    data.read( "summon_time_limit", summon_time_limit );

    // This is relative to the monster so it isn't invalidated by map shifting.
    tripoint destination;
    data.read( "destination", destination );
    goal = pos() + destination;

    upgrades = data.get_bool( "upgrades", type->upgrades );
    upgrade_time = data.get_int( "upgrade_time", -1 );

    reproduces = data.get_bool( "reproduces", type->reproduces );
    baby_timer.reset();
    data.read( "baby_timer", baby_timer );
    if( baby_timer && *baby_timer == calendar::before_time_starts ) {
        baby_timer.reset();
    }

    data.read( "udder_timer", udder_timer );

    horde_attraction = static_cast<monster_horde_attraction>( data.get_int( "horde_attraction", 0 ) );

    data.read( "inv", inv );
    data.read( "corpse_components", corpse_components );
    data.read( "dragged_foe_id", dragged_foe_id );

    if( data.has_int( "ammo" ) && !type->starting_ammo.empty() ) {
        // Legacy loading for ammo.
        normalize_ammo( data.get_int( "ammo" ) );
    } else {
        data.read( "ammo", ammo );
        // legacy loading for milkable creatures, fix mismatch.
        if( has_flag( MF_MILKABLE ) && !type->starting_ammo.empty() && !ammo.empty() &&
            type->starting_ammo.begin()->first != ammo.begin()->first ) {
            const itype_id old_type = ammo.begin()->first;
            const int old_value = ammo.begin()->second;
            ammo[type->starting_ammo.begin()->first] = old_value;
            ammo.erase( old_type );
        }
    }

    faction = mfaction_str_id( data.get_string( "faction", "" ) );
    if( !data.read( "last_updated", last_updated ) ) {
        last_updated = calendar::turn;
    }
    data.read( "mounted_player_id", mounted_player_id );
    data.read( "path", path );
}

/*
 * Save, json ed; serialization that won't break as easily. In theory.
 */
void monster::serialize( JsonOut &json ) const
{
    json.start_object();
    // This must be after the json object has been started, so any super class
    // puts their data into the same json object.
    store( json );
    json.end_object();
}

void monster::store( JsonOut &json ) const
{
    Creature::store( json );
    json.member( "typeid", type->id );
    json.member( "unique_name", unique_name );
    json.member( "posx", position.x );
    json.member( "posy", position.y );
    json.member( "posz", position.z );
    json.member( "wandx", wander_pos.x );
    json.member( "wandy", wander_pos.y );
    json.member( "wandz", wander_pos.z );
    json.member( "wandf", wandf );
    json.member( "hp", hp );
    json.member( "special_attacks", special_attacks );
    json.member( "friendly", friendly );
    json.member( "fish_population", fish_population );
    json.member( "faction", faction.id().str() );
    json.member( "mission_id", mission_id );
    json.member( "no_extra_death_drops", no_extra_death_drops );
    json.member( "dead", dead );
    json.member( "anger", anger );
    json.member( "morale", morale );
    json.member( "hallucination", hallucination );
    json.member( "aggro_character", aggro_character );
    json.member( "stairscount", staircount );
    if( tied_item ) {
        json.member( "tied_item", *tied_item );
    }
    if( tack_item ) {
        json.member( "tack_item", *tack_item );
    }
    if( armor_item ) {
        json.member( "armor_item", *armor_item );
    }
    if( storage_item ) {
        json.member( "storage_item", *storage_item );
    }
    if( battery_item ) {
        json.member( "battery_item", *battery_item );
    }
    // Store the relative position of the goal so it loads correctly after a map shift.
    json.member( "destination", goal - pos() );
    json.member( "ammo", ammo );
    json.member( "upgrades", upgrades );
    json.member( "upgrade_time", upgrade_time );
    json.member( "last_updated", last_updated );
    json.member( "reproduces", reproduces );
    json.member( "baby_timer", baby_timer );
    json.member( "udder_timer", udder_timer );

    json.member( "summon_time_limit", summon_time_limit );

    if( horde_attraction > MHA_NULL && horde_attraction < NUM_MONSTER_HORDE_ATTRACTION ) {
        json.member( "horde_attraction", horde_attraction );
    }
    json.member( "inv", inv );
    json.member( "corpse_components", corpse_components );

    json.member( "dragged_foe_id", dragged_foe_id );
    // storing the rider
    json.member( "mounted_player_id", mounted_player_id );
    json.member( "path", path );
}

void mon_special_attack::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "cooldown", cooldown );
    json.member( "enabled", enabled );
    json.end_object();
}

void time_point::serialize( JsonOut &jsout ) const
{
    jsout.write( turn_ );
}

void time_point::deserialize( JsonIn &jsin )
{
    turn_ = jsin.get_int();
}

void time_duration::serialize( JsonOut &jsout ) const
{
    jsout.write( turns_ );
}

void time_duration::deserialize( JsonIn &jsin )
{
    if( jsin.test_string() ) {
        *this = read_from_json_string<time_duration>( jsin, time_duration::units );
    } else {
        turns_ = jsin.get_int();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// item.h

void item::craft_data::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "making", making->ident().str() );
    jsout.member( "comps_used", comps_used );
    jsout.member( "next_failure_point", next_failure_point );
    jsout.member( "tools_to_continue", tools_to_continue );
    jsout.member( "cached_tool_selections", cached_tool_selections );
    jsout.end_object();
}

void item::craft_data::deserialize( JsonIn &jsin )
{
    deserialize( jsin.get_object() );
}

void item::craft_data::deserialize( const JsonObject &obj )
{
    obj.allow_omitted_members();
    making = &recipe_id( obj.get_string( "making" ) ).obj();
    obj.read( "comps_used", comps_used );
    next_failure_point = obj.get_int( "next_failure_point", -1 );
    tools_to_continue = obj.get_bool( "tools_to_continue", false );
    obj.read( "cached_tool_selections", cached_tool_selections );
}

// Template parameter because item::craft_data is private and I don't want to make it public.
template<typename T>
static void load_legacy_craft_data( io::JsonObjectInputArchive &archive, T &value )
{
    archive.allow_omitted_members();
    if( archive.has_member( "making" ) ) {
        value = cata::make_value<typename T::element_type>();
        value->deserialize( archive );
    }
}

// Dummy function as we never load anything from an output archive.
template<typename T>
static void load_legacy_craft_data( io::JsonObjectOutputArchive &, T & )
{
}

namespace charge_removal_blacklist
{
static std::set<itype_id> removal_list;

const std::set<itype_id> &get()
{
    return removal_list;
}
void load( const JsonObject &jo )
{
    std::set<std::string> d = jo.get_tags( "list" );
    for( const std::string &s : d ) {
        removal_list.insert( itype_id( s ) );
    }
}
void reset()
{
    removal_list.clear();
}
} // namespace charge_removal_blacklist

namespace to_cbc_migration
{
static std::set<itype_id> the_list;

void load( const JsonObject &jo )
{
    std::set<std::string> d = jo.get_tags( "list" );
    for( const std::string &s : d ) {
        the_list.insert( itype_id( s ) );
    }
}

void reset()
{
    the_list.clear();
}

static bool migration_required( const item &i )
{
    if( !i.count_by_charges() ) {
        return false;
    }
    return the_list.contains( i.typeId() );
}

/**
 * Merge old individual items into new count-by-charges items with same id.
 */
static void migrate( std::vector<detached_ptr<item>> &stack )
{
    for( auto it_src = stack.begin(); it_src != stack.end(); ) {
        if( !migration_required( **it_src ) ) {
            it_src++;
            continue;
        }
        auto it_dst = it_src;
        it_dst++;
        bool merged = false;
        for( ; it_dst != stack.end(); it_dst++ ) {
            detached_ptr<item> &src = *it_src;
            if( src->stacks_with( **it_dst ) && ( *it_dst )->merge_charges( std::move( src ) ) ) {
                it_src = stack.erase( it_src );
                merged = true;
                break;
            }
        }
        if( !merged ) {
            it_src++;
        }
    }
}
} // namespace to_cbc_migration

template<typename Archive>
void item::io( Archive &archive )
{

    itype_id orig; // original ID as loaded from JSON
    const auto load_type = [&]( const std::string & id ) {
        orig = itype_id( id );
        convert( item_controller->migrate_id( orig ) );
    };

    const auto load_curammo = [this]( const std::string & id ) {
        curammo = &*item_controller->migrate_id( itype_id( id ) );
    };
    const auto load_corpse = [this]( const std::string & id ) {
        if( itype_id( id ).is_null() ) {
            // backwards compatibility, nullptr should not be stored at all
            corpse = nullptr;
        } else {
            corpse = &mtype_id( id ).obj();
        }
    };
    archive.template io<const itype>( "typeid", type, load_type, []( const itype & i ) {
        return i.get_id().str();
    }, io::required_tag() );

    // normalize legacy saves to always have charges >= 0
    archive.io( "charges", charges, 0 );
    charges = std::max( charges, 0 );

    archive.io( "energy", energy, 0_J );

    archive.io( "burnt", burnt, 0 );
    archive.io( "poison", poison, 0 );
    archive.io( "frequency", frequency, 0 );
    archive.io( "snip_id", snip_id, snippet_id::NULL_ID() );
    // NB! field is named `irridation` in legacy files
    archive.io( "irridation", irradiation, 0 );
    archive.io( "bday", bday, calendar::start_of_cataclysm );
    archive.io( "mission_id", mission_id, -1 );
    archive.io( "player_id", player_id, -1 );
    archive.io( "item_vars", item_vars, io::empty_default_tag() );
    // TODO: change default to empty string
    archive.io( "name", corpse_name, std::string() );
    archive.io( "owner", owner, faction_id::NULL_ID() );
    archive.io( "old_owner", old_owner, faction_id::NULL_ID() );
    archive.io( "invlet", invlet, '\0' );
    archive.io( "damaged", damage_, 0 );
    archive.io( "active", active, false );
    if( is_tool() ) {
        archive.io( "turns_active", type->tool->turns_active, 0 );
    }
    archive.io( "is_favorite", is_favorite, false );
    archive.io( "item_counter", item_counter, static_cast<decltype( item_counter )>( 0 ) );
    archive.io( "rot", rot, 0_turns );
    archive.io( "last_rot_check", last_rot_check, calendar::start_of_cataclysm );
    archive.io( "techniques", techniques, io::empty_default_tag() );
    archive.io( "faults", faults, io::empty_default_tag() );
    archive.io( "item_tags", item_tags, io::empty_default_tag() );
    archive.io( "components", components, io::empty_default_tag() );
    archive.io( "recipe_charges", recipe_charges, 1 );
    archive.template io<const itype>( "curammo", curammo, load_curammo,
    []( const itype & i ) {
        return i.get_id().str();
    } );
    archive.template io<const mtype>( "corpse", corpse, load_corpse,
    []( const mtype & i ) {
        return i.id.str();
    } );
    archive.io( "craft_data", craft_data_, decltype( craft_data_ )() );
    archive.io( "light", light.luminance, nolight.luminance );
    archive.io( "light_width", light.width, nolight.width );
    archive.io( "light_dir", light.direction, nolight.direction );

    static const cata::value_ptr<relic> null_relic_ptr = nullptr;
    archive.io( "relic_data", relic_data, null_relic_ptr );

    archive.io( "drop_token", drop_token, decltype( drop_token )() );

    item_controller->migrate_item( orig, *this );

    if( !Archive::is_input::value ) {
        return;
    }
    /* Loading has finished, following code is to ensure consistency and fixes bugs in saves. */

    load_legacy_craft_data( archive, craft_data_ );

    double float_damage = 0;
    if( archive.read( "damage", float_damage ) ) {
        damage_ = std::min( std::max( min_damage(),
                                      static_cast<int>( float_damage * itype::damage_scale ) ),
                            max_damage() );
    }

    int note = 0;
    const bool note_read = archive.read( "note", note );

    // Old saves used to only contain one of those values (stored under "poison"), it would be
    // loaded into a union of those members. Now they are separate members and must be set separately.
    if( poison != 0 && note == 0 && !type->snippet_category.empty() ) {
        std::swap( note, poison );
    }
    if( poison != 0 && frequency == 0 && ( typeId() == itype_radio_on || typeId() == itype_radio ) ) {
        std::swap( frequency, poison );
    }
    if( poison != 0 && irradiation == 0 && typeId() == itype_rad_badge ) {
        std::swap( irradiation, poison );
    }

    // erase all invalid flags (not defined in flags.json)
    // warning was generated earlier on load
    erase_if( item_tags, [&]( const flag_id & f ) {
        return !f.is_valid();
    } );

    if( note_read ) {
        snip_id = SNIPPET.migrate_hash_to_id( note );
    } else {
        std::optional<std::string> snip;
        if( archive.read( "snippet_id", snip ) && snip ) {
            snip_id = snippet_id( snip.value() );
        }
    }

    // Compatibility for item type changes: for example soap changed from being a generic item
    // (item::charges -1 or 0 or anything else) to comestible (and thereby counted by charges),
    // old saves still have invalid charges, this fixes the charges value to the default charges.
    if( count_by_charges() && charges <= 0 ) {
        charges = item( type, calendar::start_of_cataclysm ).charges;
    }
    if( is_food() ) {
        active = true;
    }
    if( !is_active() && has_flag( flag_WET ) ) {
        // Some wet items from legacy saves may be inactive
        active = true;
    }
    std::string mode;
    if( archive.read( "mode", mode ) ) {
        // only for backward compatibility (nowadays mode is stored in item_vars)
        gun_set_mode( gun_mode_id( mode ) );
    }

    // Books without any chapters don't need to store a remaining-chapters
    // counter, it will always be 0 and it prevents proper stacking.
    if( get_chapters() == 0 ) {
        for( auto it = item_vars.begin(); it != item_vars.end(); ) {
            if( it->first.starts_with( "remaining-chapters-" ) ) {
                item_vars.erase( it++ );
            } else {
                ++it;
            }
        }
    }

    // Remove stored translated gerund in favor of storing the inscription tool type
    item_vars.erase( "item_label_type" );
    item_vars.erase( "item_note_type" );

    // Activate corpses from old saves
    if( is_corpse() && !is_active() ) {
        active = true;
    }

    if( charges != 0 && !type->can_have_charges() ) {
        // Types that are known to have charges, but should not have them.
        // We fix it here, but it's expected from bugged saves and does not require a message.
        if( !charge_removal_blacklist::get().contains( type->get_id() ) ) {
            debugmsg( "Item %s was loaded with charges, but can not have any!", type->get_id() );
        }
        charges = 0;
        curammo = nullptr;
    }

    // Relic check. Kinda late, but that's how relics have to be
    if( relic_data ) {
        relic_data->check();
    }
}

void item::deserialize( JsonIn &jsin )
{
    const JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    io::JsonObjectInputArchive archive( data );
    io( archive );
    // made for fast forwarding time from 0.D to 0.E
    if( savegame_loading_version < 27 ) {
        legacy_fast_forward_time();
    }
    if( data.has_array( "contents" ) ) {
        std::vector<detached_ptr<item>> items;
        data.read( "contents", items );
        for( detached_ptr<item> &obj : items ) {
            contents.insert_item( std::move( obj ) );
        }
    } else {
        data.read( "contents", contents );
    }
    if( data.has_member( "item_kill_tracker" ) ) {
        kills = std::make_unique<kill_tracker>( false );
        data.read( "item_kill_tracker", kills );
    }

    if( data.has_member( "id" ) ) {
        safe_reference<item>::id_type id;
        data.read( "id", id );
        safe_reference<item>::register_load( this, id );
    }

    // Sealed item migration: items with "unseals_into" set should always have contents
    if( contents.empty() && is_non_resealable_container() ) {
        convert( type->container->unseals_into );
    }
}

void item::serialize( JsonOut &json ) const
{
    io::JsonObjectOutputArchive archive( json );
    const_cast<item *>( this )->io( archive );
    if( !contents.empty() ) {
        json.member( "contents", contents );
    }
    if( kills ) {
        json.member( "item_kill_tracker" );
        kills->serialize( json );
    }

    safe_reference<item>::id_type id = safe_reference<item>::lookup_id( this );
    if( id != safe_reference<item>::ID_NONE ) {
        json.member( "id", id );
    }

}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// vehicle.h

/*
 * vehicle_part
 */
void vehicle_part::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    vpart_id pid;
    data.read( "id", pid );

    std::map<std::string, std::pair<std::string, std::string>> deprecated = {
        { "laser_gun", { "laser_rifle", "none" } },
        { "seat_nocargo", { "seat", "none" } },
        { "engine_plasma", { "minireactor", "none" } },
        { "battery_truck", { "battery_car", "battery" } },

        { "diesel_tank_little", { "tank_little", "diesel" } },
        { "diesel_tank_small", { "tank_small", "diesel" } },
        { "diesel_tank_medium", { "tank_medium", "diesel" } },
        { "diesel_tank", { "tank", "diesel" } },
        { "external_diesel_tank_small", { "external_tank_small", "diesel" } },
        { "external_diesel_tank", { "external_tank", "diesel" } },

        { "gas_tank_little", { "tank_little", "gasoline" } },
        { "gas_tank_small", { "tank_small", "gasoline" } },
        { "gas_tank_medium", { "tank_medium", "gasoline" } },
        { "gas_tank", { "tank", "gasoline" } },
        { "external_gas_tank_small", { "external_tank_small", "gasoline" } },
        { "external_gas_tank", { "external_tank", "gasoline" } },

        { "water_dirty_tank_little", { "tank_little", "water" } },
        { "water_dirty_tank_small", { "tank_small", "water" } },
        { "water_dirty_tank_medium", { "tank_medium", "water" } },
        { "water_dirty_tank", { "tank", "water" } },
        { "external_water_dirty_tank_small", { "external_tank_small", "water" } },
        { "external_water_dirty_tank", { "external_tank", "water" } },
        { "dirty_water_tank_barrel", { "tank_barrel", "water" } },

        { "water_tank_little", { "tank_little", "water_clean" } },
        { "water_tank_small", { "tank_small", "water_clean" } },
        { "water_tank_medium", { "tank_medium", "water_clean" } },
        { "water_tank", { "tank", "water_clean" } },
        { "external_water_tank_small", { "external_tank_small", "water_clean" } },
        { "external_water_tank", { "external_tank", "water_clean" } },
        { "water_tank_barrel", { "tank_barrel", "water_clean" } },

        { "napalm_tank", { "tank", "napalm" } },

        { "hydrogen_tank", { "tank", "none" } }
    };

    // required for compatibility with 0.C saves
    itype_id legacy_fuel;

    auto dep = deprecated.find( pid.str() );
    if( dep != deprecated.end() ) {
        pid = vpart_id( dep->second.first );
        legacy_fuel = itype_id( dep->second.second );
    }

    // if we don't know what type of part it is, it'll cause problems later.
    if( !pid.is_valid() ) {
        if( pid.str() == "wheel_underbody" ) {
            pid = vpart_id( "wheel_wide" );
        } else {
            data.throw_error( "bad vehicle part", "id" );
        }
    }
    id = pid;

    if( data.has_object( "base" ) ) {
        data.read( "base", base );
    } else {
        // handle legacy format which didn't include the base item
        set_base( item::spawn( id.obj().item ) );
    }

    data.read( "mount_dx", mount.x );
    data.read( "mount_dy", mount.y );
    data.read( "open", open );
    int direction_int;
    data.read( "direction", direction_int );
    direction = units::from_degrees( direction_int );
    data.read( "blood", blood );
    data.read( "proxy_part_id", proxy_part_id );
    data.read( "proxy_sym", proxy_sym );
    data.read( "enabled", enabled );
    data.read( "flags", flags );
    data.read( "passenger_id", passenger_id );
    if( data.has_int( "z_offset" ) ) {
        int z_offset = data.get_int( "z_offset" );
        if( std::abs( z_offset ) > 10 ) {
            data.throw_error( "z_offset out of range", "z_offset" );
        }
        precalc[0].z = z_offset;
        precalc[1].z = z_offset;
    }
    JsonArray ja = data.get_array( "carry" );
    size_t sz = ja.size();
    for( size_t index = 0; index < sz; index++ ) {
        carry_names.push( ja.get_string( sz - index - 1 ) );
    }
    data.read( "crew_id", crew_id );
    data.read( "items", items );
    data.read( "target_first_x", target.first.x );
    data.read( "target_first_y", target.first.y );
    data.read( "target_first_z", target.first.z );
    data.read( "target_second_x", target.second.x );
    data.read( "target_second_y", target.second.y );
    data.read( "target_second_z", target.second.z );
    data.read( "ammo_pref", ammo_pref );

    if( legacy_fuel.is_empty() ) {
        legacy_fuel = id.obj().fuel_type;
    }

    // with VEHICLE tag migrate fuel tanks only if amount field exists
    if( base->has_flag( flag_id( "VEHICLE" ) ) ) {
        if( data.has_int( "amount" ) && ammo_capacity() > 0 && legacy_fuel != itype_battery ) {
            ammo_set( legacy_fuel, data.get_int( "amount" ) );
        }

        // without VEHICLE flag always migrate both batteries and fuel tanks
    } else {
        if( ammo_capacity() > 0 ) {
            ammo_set( legacy_fuel, data.get_int( "amount" ) );
        }
        base->item_tags.insert( flag_id( "VEHICLE" ) );
    }

    if( data.has_int( "hp" ) && id.obj().durability > 0 ) {
        // migrate legacy savegames exploiting that all base items at that time had max_damage() of 4
        base->set_damage( 4 * itype::damage_scale - 4 * itype::damage_scale * data.get_int( "hp" ) /
                          id.obj().durability );
    }

    // legacy turrets loaded ammo via a pseudo CARGO space
    if( is_turret() && !items.empty() ) {
        const int qty = std::accumulate( items.begin(), items.end(), 0, []( int lhs,
        const item * const & rhs ) {
            return lhs + rhs->charges;
        } );
        ammo_set( ( *items.begin() )->ammo_current(), qty );
        items.clear();
    }
}

void vehicle_part::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "id", id.str() );
    json.member( "base", base );
    json.member( "mount_dx", mount.x );
    json.member( "mount_dy", mount.y );
    json.member( "open", open );
    json.member( "direction", std::lround( to_degrees( direction ) ) );
    json.member( "blood", blood );
    json.member( "proxy_part_id", proxy_part_id );
    json.member( "proxy_sym", proxy_sym );
    json.member( "enabled", enabled );
    json.member( "flags", flags );
    if( !carry_names.empty() ) {
        std::stack<std::string, std::vector<std::string> > carry_copy = carry_names;
        json.member( "carry" );
        json.start_array();
        while( !carry_copy.empty() ) {
            json.write( carry_copy.top() );
            carry_copy.pop();
        }
        json.end_array();
    }
    json.member( "passenger_id", passenger_id );
    json.member( "crew_id", crew_id );
    if( precalc[0].z ) {
        json.member( "z_offset", precalc[0].z );
    }
    json.member( "items", items );
    if( target.first != tripoint_min ) {
        json.member( "target_first_x", target.first.x );
        json.member( "target_first_y", target.first.y );
        json.member( "target_first_z", target.first.z );
    }
    if( target.second != tripoint_min ) {
        json.member( "target_second_x", target.second.x );
        json.member( "target_second_y", target.second.y );
        json.member( "target_second_z", target.second.z );
    }
    json.member( "ammo_pref", ammo_pref );
    json.end_object();
}

/*
 * label
 */
void label::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "x", x );
    data.read( "y", y );
    data.read( "text", text );
}

void label::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "x", x );
    json.member( "y", y );
    json.member( "text", text );
    json.end_object();
}

/*
 * Load vehicle from a json blob that might just exceed player in size.
 */
void vehicle::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();

    int fdir = 0;
    int mdir = 0;

    data.read( "type", type );
    data.read( "posx", pos.x );
    data.read( "posy", pos.y );
    data.read( "om_id", om_id );
    data.read( "faceDir", fdir );
    data.read( "moveDir", mdir );
    int turn_dir_int;
    data.read( "turn_dir", turn_dir_int );
    turn_dir = units::from_degrees( turn_dir_int );
    data.read( "velocity", velocity );
    data.read( "falling", is_falling );
    data.read( "floating", is_floating );
    data.read( "flying", is_flying );
    data.read( "cruise_velocity", cruise_velocity );
    data.read( "vertical_velocity", vertical_velocity );
    data.read( "cruise_on", cruise_on );
    data.read( "engine_on", engine_on );
    data.read( "tracking_on", tracking_on );
    data.read( "skidding", skidding );
    data.read( "of_turn_carry", of_turn_carry );
    data.read( "is_locked", is_locked );
    data.read( "is_alarm_on", is_alarm_on );
    data.read( "camera_on", camera_on );
    if( !data.read( "last_update_turn", last_update ) ) {
        last_update = calendar::turn;
    }

    units::angle fdir_angle = units::from_degrees( fdir );
    face.init( fdir_angle );
    move.init( units::from_degrees( mdir ) );
    data.read( "name", name );
    std::string temp_id;
    std::string temp_old_id;
    data.read( "owner", temp_id );
    data.read( "old_owner", temp_old_id );
    // for savegames before the change to faction_id for ownership.
    if( temp_id.empty() ) {
        owner = faction_id::NULL_ID();
    } else {
        owner = faction_id( temp_id );
    }
    if( temp_old_id.empty() ) {
        old_owner = faction_id::NULL_ID();
    } else {
        old_owner = faction_id( temp_old_id );
    }
    data.read( "theft_time", theft_time );

    data.read( "parts", parts );

    // we persist the pivot anchor so that if the rules for finding
    // the pivot change, existing vehicles do not shift around.
    // Loading vehicles that predate the pivot logic is a special
    // case of this, they will load with an anchor of (0,0) which
    // is what they're expecting.
    data.read( "pivot", pivot_anchor[0] );
    pivot_anchor[1] = pivot_anchor[0];
    pivot_rotation[1] = pivot_rotation[0] = fdir_angle;
    data.read( "is_following", is_following );
    data.read( "is_patrolling", is_patrolling );
    data.read( "autodrive_local_target", autodrive_local_target );
    data.read( "summon_time_limit", summon_time_limit );
    data.read( "magic", magic );

    // Loose items -> count-by-charges migration
    for( vehicle_part &part : parts ) {
        //TODO!: this is awful
        std::vector<detached_ptr<item>> clears = part.clear_items();

        to_cbc_migration::migrate( clears );
        part.set_vehicle_hack( this );
        for( detached_ptr<item> &it :  clears ) {
            part.add_item( std::move( it ) );
        }
    }

    // Need to manually backfill the active item cache since the part loader can't call its vehicle.
    for( const vpart_reference &vp : get_any_parts( VPFLAG_CARGO ) ) {
        auto it = vp.part().items.begin();
        auto end = vp.part().items.end();
        for( ; it != end; ++it ) {
            if( ( *it )->needs_processing() ) {
                active_items.add( **it );
            }
        }
    }

    for( const vpart_reference &vp : get_any_parts( "TURRET" ) ) {
        install_part( vp.mount(), vpart_id( "turret_mount" ), false );

        //Forcibly set turrets' targeting mode to manual if no turret control unit is present on turret's tile on loading save
        if( !has_part( global_part_pos3( vp.part() ), "TURRET_CONTROLS" ) ) {
            vp.part().enabled = false;
        }
        //Set turret control unit's state equal to turret's targeting mode on loading save
        for( const vpart_reference &turret_part : get_any_parts( "TURRET_CONTROLS" ) ) {
            turret_part.part().enabled = vp.part().enabled;
        }
    }

    // Add vehicle mounts to cars that are missing them.
    for( const vpart_reference &vp : get_any_parts( "NEEDS_WHEEL_MOUNT_LIGHT" ) ) {
        if( vp.info().has_flag( "STEERABLE" ) ) {
            install_part( vp.mount(), vpart_id( "wheel_mount_light_steerable" ), false );
        } else {
            install_part( vp.mount(), vpart_id( "wheel_mount_light" ), false );
        }
    }
    for( const vpart_reference &vp : get_any_parts( "NEEDS_WHEEL_MOUNT_MEDIUM" ) ) {
        if( vp.info().has_flag( "STEERABLE" ) ) {
            install_part( vp.mount(), vpart_id( "wheel_mount_medium_steerable" ), false );
        } else {
            install_part( vp.mount(), vpart_id( "wheel_mount_medium" ), false );
        }
    }
    for( const vpart_reference &vp : get_any_parts( "NEEDS_WHEEL_MOUNT_HEAVY" ) ) {
        if( vp.info().has_flag( "STEERABLE" ) ) {
            install_part( vp.mount(), vpart_id( "wheel_mount_heavy_steerable" ), false );
        } else {
            install_part( vp.mount(), vpart_id( "wheel_mount_heavy" ), false );
        }
    }

    /* After loading, check if the vehicle is from the old rules and is missing
     * frames. */
    if( savegame_loading_version < 11 ) {
        add_missing_frames();
    }

    // Handle steering changes
    if( savegame_loading_version < 25 ) {
        add_steerable_wheels();
    }

    refresh();

    data.read( "tags", tags );
    data.read( "labels", labels );

    point p;
    zone_data zd;
    for( JsonObject sdata : data.get_array( "zones" ) ) {
        sdata.allow_omitted_members();
        sdata.read( "point", p );
        sdata.read( "zone", zd );
        loot_zones.emplace( p, zd );
    }
    data.read( "other_tow_point", tow_data.other_towing_point );
    // Note that it's possible for a vehicle to be loaded midway
    // through a turn if the player is driving REALLY fast and their
    // own vehicle motion takes them in range. An undefined value for
    // on_turn caused occasional weirdness if the undefined value
    // happened to be positive.
    //
    // Setting it to zero means it won't get to move until the start
    // of the next turn, which is what happens anyway if it gets
    // loaded anywhere but midway through a driving cycle.
    //
    // Something similar to vehicle::gain_moves() would be ideal, but
    // that can't be used as it currently stands because it would also
    // make it instantly fire all its turrets upon load.
    of_turn = 0;

    /** Legacy saved games did not store part enabled status within parts */
    const auto set_legacy_state = [&]( const std::string & var, const std::string & flag ) {
        if( data.get_bool( var, false ) ) {
            for( const vpart_reference &vp : get_any_parts( flag ) ) {
                vp.part().enabled = true;
            }
        }
    };

    set_legacy_state( "stereo_on", "STEREO" );
    set_legacy_state( "chimes_on", "CHIMES" );
    set_legacy_state( "fridge_on", "FRIDGE" );
    set_legacy_state( "reaper_on", "REAPER" );
    set_legacy_state( "planter_on", "PLANTER" );
    set_legacy_state( "recharger_on", "RECHARGE" );
    set_legacy_state( "scoop_on", "SCOOP" );
    set_legacy_state( "plow_on", "PLOW" );
    set_legacy_state( "reactor_on", "REACTOR" );
}

void vehicle::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", type );
    json.member( "posx", pos.x );
    json.member( "posy", pos.y );
    json.member( "om_id", om_id );
    json.member( "faceDir", std::lround( to_degrees( face.dir() ) ) );
    json.member( "moveDir", std::lround( to_degrees( move.dir() ) ) );
    json.member( "turn_dir", std::lround( to_degrees( turn_dir ) ) );
    json.member( "velocity", velocity );
    json.member( "falling", is_falling );
    json.member( "floating", is_floating );
    json.member( "flying", is_flying );
    json.member( "cruise_velocity", cruise_velocity );
    json.member( "vertical_velocity", vertical_velocity );
    json.member( "cruise_on", cruise_on );
    json.member( "engine_on", engine_on );
    json.member( "tracking_on", tracking_on );
    json.member( "skidding", skidding );
    json.member( "of_turn_carry", of_turn_carry );
    json.member( "name", name );
    json.member( "owner", owner );
    json.member( "old_owner", old_owner );
    json.member( "theft_time", theft_time );
    json.member( "parts", parts );
    json.member( "tags", tags );
    json.member( "labels", labels );
    json.member( "zones" );
    json.start_array();
    for( auto const &z : loot_zones ) {
        json.start_object();
        json.member( "point", z.first );
        json.member( "zone", z.second );
        json.end_object();
    }
    json.end_array();
    tripoint other_tow_temp_point;
    if( is_towed() ) {
        vehicle *tower = tow_data.get_towed_by();
        if( tower ) {
            other_tow_temp_point = tower->global_part_pos3( tower->get_tow_part() );
        }
    }
    json.member( "other_tow_point", other_tow_temp_point );

    json.member( "is_locked", is_locked );
    json.member( "is_alarm_on", is_alarm_on );
    json.member( "camera_on", camera_on );
    json.member( "last_update_turn", last_update );
    json.member( "pivot", pivot_anchor[0] );
    json.member( "is_following", is_following );
    json.member( "is_patrolling", is_patrolling );
    json.member( "autodrive_local_target", autodrive_local_target );
    json.member( "summon_time_limit", summon_time_limit );
    json.member( "magic", magic );
    json.end_object();
}

////////////////// mission.h
////
void mission::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();

    if( jo.has_int( "type_id" ) ) {
        type = &mission_type::from_legacy( jo.get_int( "type_id" ) ).obj();
    } else if( jo.has_string( "type_id" ) ) {
        type = &mission_type_id( jo.get_string( "type_id" ) ).obj();
    } else {
        debugmsg( "Saved mission has no type" );
        type = &mission_type::get_all().front();
    }

    bool failed;
    bool was_started;
    std::string status_string;
    if( jo.read( "status", status_string ) ) {
        status = status_from_string( status_string );
    } else if( jo.read( "failed", failed ) && failed ) {
        status = mission_status::failure;
    } else if( jo.read( "was_started", was_started ) && !was_started ) {
        status = mission_status::yet_to_start;
    } else {
        // Note: old code had no idea of successful missions!
        // We can't check properly here, since most of the game isn't loaded
        status = mission_status::in_progress;
    }

    jo.read( "value", value );
    jo.read( "kill_count_to_reach", kill_count_to_reach );
    jo.read( "reward", reward );
    jo.read( "uid", uid );
    JsonArray ja = jo.get_array( "target" );
    if( ja.size() == 3 ) {
        target.x() = ja.get_int( 0 );
        target.y() = ja.get_int( 1 );
        target.z() = ja.get_int( 2 );
    } else if( ja.size() == 2 ) {
        target.x() = ja.get_int( 0 );
        target.y() = ja.get_int( 1 );
    }

    if( jo.has_int( "follow_up" ) ) {
        follow_up = mission_type::from_legacy( jo.get_int( "follow_up" ) );
    } else if( jo.has_string( "follow_up" ) ) {
        follow_up = mission_type_id( jo.get_string( "follow_up" ) );
    }

    jo.read( "item_id", item_id );
    jo.read( "target_id", target_id );

    if( jo.has_int( "recruit_class" ) ) {
        recruit_class = npc_class::from_legacy_int( jo.get_int( "recruit_class" ) );
    } else {
        recruit_class = npc_class_id( jo.get_string( "recruit_class", "NC_NONE" ) );
    }

    jo.read( "target_npc_id", target_npc_id );
    jo.read( "monster_type", monster_type );
    jo.read( "monster_species", monster_species );
    jo.read( "monster_kill_goal", monster_kill_goal );
    jo.read( "deadline", deadline );
    jo.read( "step", step );
    jo.read( "item_count", item_count );
    jo.read( "npc_id", npc_id );
    jo.read( "good_fac_id", good_fac_id );
    jo.read( "bad_fac_id", bad_fac_id );

    // Suppose someone had two living players in an 0.C stable world. When loading player 1 in 0.D
    // (or maybe even creating a new player), the former condition makes legacy_no_player_id true.
    // When loading player 2, there will be a player_id member in SAVE_MASTER (i.e. master.gsav),
    // but the bool member legacy_no_player_id will have been saved as true
    // (unless the mission belongs to a player that's been loaded into 0.D)
    // See player::deserialize and mission::set_player_id_legacy_0c
    legacy_no_player_id = !jo.read( "player_id", player_id ) ||
                          jo.get_bool( "legacy_no_player_id", false );
}

void mission::serialize( JsonOut &json ) const
{
    json.start_object();

    json.member( "type_id", type->id );
    json.member( "status", status_to_string( status ) );
    json.member( "value", value );
    json.member( "kill_count_to_reach", kill_count_to_reach );
    json.member( "reward", reward );
    json.member( "uid", uid );

    json.member( "target" );
    json.start_array();
    json.write( target.x() );
    json.write( target.y() );
    json.write( target.z() );
    json.end_array();

    json.member( "item_id", item_id );
    json.member( "item_count", item_count );
    json.member( "target_id", target_id );
    json.member( "recruit_class", recruit_class );
    json.member( "target_npc_id", target_npc_id );
    json.member( "monster_type", monster_type );
    json.member( "monster_species", monster_species );
    json.member( "monster_kill_goal", monster_kill_goal );
    json.member( "deadline", deadline );
    json.member( "npc_id", npc_id );
    json.member( "good_fac_id", good_fac_id );
    json.member( "bad_fac_id", bad_fac_id );
    json.member( "step", step );
    json.member( "follow_up", follow_up );
    json.member( "player_id", player_id );
    json.member( "legacy_no_player_id", legacy_no_player_id );

    json.end_object();
}

////////////////// faction.h
////
void faction::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();

    jo.read( "id", id );
    jo.read( "name", name );
    jo.read( "likes_u", likes_u );
    jo.read( "respects_u", respects_u );
    jo.read( "known_by_u", known_by_u );
    jo.read( "size", size );
    jo.read( "power", power );
    if( !jo.read( "food_supply", food_supply ) ) {
        food_supply = 100;
    }
    if( !jo.read( "wealth", wealth ) ) {
        wealth = 100;
    }
    if( jo.has_array( "opinion_of" ) ) {
        opinion_of = jo.get_int_array( "opinion_of" );
    }
    load_relations( jo );
}

void faction::serialize( JsonOut &json ) const
{
    json.start_object();

    json.member( "id", id );
    json.member( "name", name );
    json.member( "likes_u", likes_u );
    json.member( "respects_u", respects_u );
    json.member( "known_by_u", known_by_u );
    json.member( "size", size );
    json.member( "power", power );
    json.member( "food_supply", food_supply );
    json.member( "wealth", wealth );
    json.member( "opinion_of", opinion_of );
    json.member( "relations" );
    json.start_object();
    for( const auto &rel_data : relations ) {
        json.member( rel_data.first );
        json.start_object();
        for( const auto &rel_flag : npc_factions::relation_strs ) {
            json.member( rel_flag.first, rel_data.second.test( rel_flag.second ) );
        }
        json.end_object();
    }
    json.end_object();

    json.end_object();
}

void Creature::store( JsonOut &jsout ) const
{
    jsout.member( "moves", moves );
    jsout.member( "pain", pain );

    // killer is not stored, it's temporary anyway, any creature that has a non-null
    // killer is dead (as per definition) and should not be stored.

    // Because JSON requires string keys we need to convert our int keys
    std::unordered_map<std::string, std::unordered_map<std::string, effect>> tmp_map;
    for( const auto &maps : *effects ) {
        for( const auto &i : maps.second ) {
            if( i.second.is_removed() ) {
                continue;
            }
            std::ostringstream convert;
            convert << i.first->token;
            tmp_map[maps.first.str()][convert.str()] = i.second;
        }
    }
    jsout.member( "effects", tmp_map );

    jsout.member( "values", values );

    jsout.member( "blocks_left", num_blocks );
    jsout.member( "dodges_left", num_dodges );
    jsout.member( "num_blocks_bonus", num_blocks_bonus );
    jsout.member( "num_dodges_bonus", num_dodges_bonus );

    jsout.member( "armor_bash_bonus", armor_bash_bonus );
    jsout.member( "armor_cut_bonus", armor_cut_bonus );
    jsout.member( "armor_bullet_bonus", armor_bullet_bonus );

    jsout.member( "speed", speed_base );

    jsout.member( "speed_bonus", speed_bonus );
    jsout.member( "dodge_bonus", dodge_bonus );
    jsout.member( "block_bonus", block_bonus );
    jsout.member( "hit_bonus", hit_bonus );
    jsout.member( "bash_bonus", bash_bonus );
    jsout.member( "cut_bonus", cut_bonus );
    jsout.member( "size_bonus", size_bonus );

    jsout.member( "underwater", underwater );

    jsout.member( "body", body );

    // fake is not stored, it's temporary anyway, only used to fire with a gun.
}

void Creature::load( const JsonObject &jsin )
{
    jsin.allow_omitted_members();
    jsin.read( "moves", moves );
    jsin.read( "pain", pain );

    killer.reset();

    if( jsin.has_object( "effects" ) ) {
        // Because JSON requires string keys we need to convert back to our bp keys
        std::unordered_map<std::string, std::unordered_map<std::string, effect>> tmp_map;
        jsin.read( "effects", tmp_map );
        int key_num = 0;
        for( const auto &maps : tmp_map ) {
            const efftype_id id( maps.first );
            if( !id.is_valid() ) {
                debugmsg( "Invalid effect: %s", id.c_str() );
                continue;
            }
            for( const auto &i : maps.second ) {
                if( !( std::istringstream( i.first ) >> key_num ) ) {
                    key_num = 0;
                }
                const bodypart_str_id &bp = convert_bp( static_cast<body_part>( key_num ) );
                const effect &e = i.second;

                ( *effects )[id][bp] = e;
                on_effect_int_change( id, e.get_intensity(), bp );
            }
        }
    }
    jsin.read( "values", values );

    jsin.read( "blocks_left", num_blocks );
    jsin.read( "dodges_left", num_dodges );
    jsin.read( "num_blocks_bonus", num_blocks_bonus );
    jsin.read( "num_dodges_bonus", num_dodges_bonus );

    jsin.read( "armor_bash_bonus", armor_bash_bonus );
    jsin.read( "armor_cut_bonus", armor_cut_bonus );
    jsin.read( "armor_bullet_bonus", armor_bullet_bonus );

    jsin.read( "speed", speed_base );

    jsin.read( "speed_bonus", speed_bonus );
    jsin.read( "dodge_bonus", dodge_bonus );
    jsin.read( "block_bonus", block_bonus );
    jsin.read( "hit_bonus", hit_bonus );
    jsin.read( "bash_bonus", bash_bonus );
    jsin.read( "cut_bonus", cut_bonus );
    jsin.read( "size_bonus", size_bonus );

    jsin.read( "underwater", underwater );

    jsin.read( "body", body );

    for( auto &it : body ) {
        it.second.set_location( new wield_item_location( this ) );
    }

    fake = false; // see Creature::load

    on_stat_change( "pain", pain );
}

void player_morale::morale_subtype::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member_as_string( "subtype_type", subtype_type );
    switch( subtype_type ) {
        case morale_subtype_t::single:
            break;
        case morale_subtype_t::by_item:
            json.member( "item_type", item_type->get_id() );
            break;
        case morale_subtype_t::by_effect:
            json.member( "eff_type", eff_type );
            break;
        default:
            break;
    }
    json.end_object();
}

void player_morale::morale_subtype::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "subtype_type", subtype_type );
    switch( subtype_type ) {
        case morale_subtype_t::single:
            break;
        case morale_subtype_t::by_item:
            item_type = &*itype_id( jo.get_string( "item_type" ) );
            break;
        case morale_subtype_t::by_effect:
            eff_type = efftype_id( jo.get_string( "eff_type" ) );
            break;
        default:
            debugmsg( "invalid or missing morale_subtype_t: %d",
                      static_cast<int>( subtype_type ) );
            subtype_type = morale_subtype_t::single;
    }
}

void player_morale::morale_point::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", type );
    if( !jo.read( "subtype", subtype ) && jo.has_string( "item_type" ) ) {
        subtype = morale_subtype( *itype_id( jo.get_string( "item_type" ) ) );
    }
    jo.read( "bonus", bonus );
    jo.read( "duration", duration );
    jo.read( "decay_start", decay_start );
    jo.read( "age", age );
}

void player_morale::morale_point::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", type );
    json.member( "subtype", subtype );
    json.member( "bonus", bonus );
    json.member( "duration", duration );
    json.member( "decay_start", decay_start );
    json.member( "age", age );
    json.end_object();
}

void player_morale::store( JsonOut &jsout ) const
{
    jsout.member( "morale", points );
}

void player_morale::load( const JsonObject &jsin )
{
    jsin.allow_omitted_members();
    jsin.read( "morale", points );
}

struct mm_elem {
    memorized_terrain_tile tile;
    int symbol;

    bool operator==( const mm_elem &rhs ) const {
        return symbol == rhs.symbol && tile == rhs.tile;
    }
};

void mm_submap::serialize( JsonOut &jsout ) const
{
    jsout.start_array();

    // Uses RLE for compression.

    mm_elem last;
    int num_same = 1;

    const auto write_seq = [&]() {
        jsout.start_array();
        jsout.write( last.tile.tile );
        jsout.write( last.tile.subtile );
        jsout.write( last.tile.rotation );
        jsout.write( last.symbol );
        if( num_same != 1 ) {
            jsout.write( num_same );
        }
        jsout.end_array();
    };

    for( size_t y = 0; y < SEEY; y++ ) {
        for( size_t x = 0; x < SEEX; x++ ) {
            point p( x, y );
            const mm_elem elem = { tile( p ), symbol( p ) };
            if( x == 0 && y == 0 ) {
                last = elem;
                continue;
            }
            if( last == elem ) {
                num_same += 1;
                continue;
            }
            write_seq();
            num_same = 1;
            last = elem;
        }
    }
    write_seq();

    jsout.end_array();
}

void mm_submap::deserialize( JsonIn &jsin )
{
    jsin.start_array();

    // Uses RLE for compression.

    mm_elem elem;
    size_t remaining = 0;

    for( size_t y = 0; y < SEEY; y++ ) {
        for( size_t x = 0; x < SEEX; x++ ) {
            if( remaining > 0 ) {
                remaining -= 1;
            } else {
                jsin.start_array();
                elem.tile.tile = jsin.get_string();
                elem.tile.subtile = jsin.get_int();
                elem.tile.rotation = jsin.get_int();
                elem.symbol = jsin.get_int();
                if( jsin.test_int() ) {
                    remaining = jsin.get_int() - 1;
                }
                jsin.end_array();
            }
            point p( x, y );
            // Try to avoid assigning to save up on memory
            if( elem.tile != mm_submap::default_tile ) {
                set_tile( p, elem.tile );
            }
            if( elem.symbol != mm_submap::default_symbol ) {
                set_symbol( p, elem.symbol );
            }
        }
    }
    jsin.end_array();
}

void mm_region::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
    for( size_t y = 0; y < MM_REG_SIZE; y++ ) {
        // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
        for( size_t x = 0; x < MM_REG_SIZE; x++ ) {
            const shared_ptr_fast<mm_submap> &sm = submaps[x][y];
            if( sm->is_empty() ) {
                jsout.write_null();
            } else {
                sm->serialize( jsout );
            }
        }
    }
    jsout.end_array();
}

void mm_region::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
    for( size_t y = 0; y < MM_REG_SIZE; y++ ) {
        // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
        for( size_t x = 0; x < MM_REG_SIZE; x++ ) {
            shared_ptr_fast<mm_submap> &sm = submaps[x][y];
            sm = make_shared_fast<mm_submap>();
            if( jsin.test_null() ) {
                jsin.skip_null();
            } else {
                sm->deserialize( jsin );
            }
        }
    }
    jsin.end_array();
}

void map_memory::load_legacy( JsonIn &jsin )
{
    struct mig_elem {
        int symbol;
        memorized_terrain_tile tile;
    };
    std::map<tripoint, mig_elem> elems;

    jsin.start_array();
    jsin.start_array();
    while( !jsin.end_array() ) {
        jsin.start_array();
        tripoint p;
        p.x = jsin.get_int();
        p.y = jsin.get_int();
        p.z = jsin.get_int();
        mig_elem &elem = elems[p];
        elem.tile.tile = jsin.get_string();
        elem.tile.subtile = jsin.get_int();
        elem.tile.rotation = jsin.get_int();
        jsin.end_array();
    }
    jsin.start_array();
    while( !jsin.end_array() ) {
        jsin.start_array();
        tripoint p;
        p.x = jsin.get_int();
        p.y = jsin.get_int();
        p.z = jsin.get_int();
        elems[p].symbol = jsin.get_int();
        jsin.end_array();
    }
    jsin.end_array();

    for( const std::pair<const tripoint, mig_elem> &elem : elems ) {
        coord_pair cp( elem.first );
        shared_ptr_fast<mm_submap> sm = find_submap( cp.sm );
        if( !sm ) {
            sm = allocate_submap( cp.sm );
        }
        if( elem.second.tile != mm_submap::default_tile ) {
            sm->set_tile( cp.loc, elem.second.tile );
        }
        if( elem.second.symbol != mm_submap::default_symbol ) {
            sm->set_symbol( cp.loc, elem.second.symbol );
        }
    }
}

void point::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    x = jsin.get_int();
    y = jsin.get_int();
    jsin.end_array();
}

void point::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( x );
    jsout.write( y );
    jsout.end_array();
}

void tripoint::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    x = jsin.get_int();
    y = jsin.get_int();
    z = jsin.get_int();
    jsin.end_array();
}

void tripoint::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( x );
    jsout.write( y );
    jsout.write( z );
    jsout.end_array();
}

void addiction::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type_enum", type );
    json.member( "intensity", intensity );
    json.member( "sated", sated );
    json.end_object();
}

void addiction::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    type = static_cast<add_type>( jo.get_int( "type_enum" ) );
    intensity = jo.get_int( "intensity" );
    jo.read( "sated", sated );
}

void serialize( const recipe_subset &value, JsonOut &jsout )
{
    jsout.start_array();
    for( const auto &entry : value ) {
        jsout.write( entry->ident() );
    }
    jsout.end_array();
}

void deserialize( recipe_subset &value, JsonIn &jsin )
{
    value.clear();
    jsin.start_array();
    while( !jsin.end_array() ) {
        value.include( &recipe_id( jsin.get_string() ).obj() );
    }
}

static void serialize( const item_comp &value, JsonOut &jsout )
{
    jsout.start_object();

    jsout.member( "type", value.type );
    jsout.member( "count", value.count );
    jsout.member( "recoverable", value.recoverable );

    jsout.end_object();
}

static void serialize( const tool_comp &value, JsonOut &jsout )
{
    jsout.start_object();

    jsout.member( "type", value.type );
    jsout.member( "count", value.count );
    jsout.member( "recoverable", value.recoverable );

    jsout.end_object();
}

static void deserialize( item_comp &value, JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", value.type );
    jo.read( "count", value.count );
    jo.read( "recoverable", value.recoverable );
}

static void deserialize( tool_comp &value, JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", value.type );
    jo.read( "count", value.count );
    jo.read( "recoverable", value.recoverable );
}

static void serialize( const quality_requirement &value, JsonOut &jsout )
{
    jsout.start_object();

    jsout.member( "type", value.type );
    jsout.member( "count", value.count );
    jsout.member( "level", value.level );

    jsout.end_object();
}

static void deserialize( quality_requirement &value, JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", value.type );
    jo.read( "count", value.count );
    jo.read( "level", value.level );
}

void iuse_location::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( loc );
    jsout.write( count );
    jsout.end_array();
}

void iuse_location::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    jsin.read( loc );
    jsin.read( count );
    jsin.end_array();
}

void pickup::act_item::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( loc );
    jsout.write( count );
    jsout.write( consumed_moves );
    jsout.end_array();
}

void pickup::act_item::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    jsin.read( loc );
    jsin.read( count );
    jsin.read( consumed_moves );
    jsin.end_array();
}

void kill_tracker::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "kills" );
    jsout.start_object();
    for( auto &elem : kills ) {
        jsout.member( elem.first.str(), elem.second );
    }
    jsout.end_object();

    jsout.member( "npc_kills" );
    jsout.start_array();
    for( auto &elem : npc_kills ) {
        jsout.write( elem );
    }
    jsout.end_array();
    jsout.end_object();
}

void kill_tracker::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    for( const JsonMember member : data.get_object( "kills" ) ) {
        kills[mtype_id( member.name() )] = member.get_int();
    }

    for( const std::string npc_name : data.get_array( "npc_kills" ) ) {
        npc_kills.push_back( npc_name );
    }
}

void cata_variant::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write_as_string( type_ );
    jsout.write( value_ );
    jsout.end_array();
}

void cata_variant::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    if( !( jsin.read( type_ ) && jsin.read( value_ ) ) ) {
        jsin.error( "Failed to read cata_variant" );
    }
    jsin.end_array();
}

void event_multiset::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    std::vector<counts_type::value_type> copy( counts_.begin(), counts_.end() );
    jsout.member( "event_counts", copy );
    jsout.end_object();
}

void event_multiset::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    std::vector<std::pair<cata::event::data_type, int>> copy;
    jo.read( "event_counts", copy );
    counts_ = { copy.begin(), copy.end() };
}

void stats_tracker::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "data", data );
    jsout.member( "initial_scores", initial_scores );
    jsout.end_object();
}

void stats_tracker::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "data", data );
    for( std::pair<const event_type, event_multiset> &d : data ) {
        d.second.set_type( d.first );
    }
    jo.read( "initial_scores", initial_scores );
}

void submap::store( JsonOut &jsout ) const
{
    jsout.member( "turn_last_touched", last_touched );
    jsout.member( "temperature", temperature );

    // Terrain is saved using a simple RLE scheme.  Legacy saves don't have
    // this feature but the algorithm is backward compatible.
    jsout.member( "terrain" );
    jsout.start_array();
    std::string last_id;
    int num_same = 1;
    for( int j = 0; j < SEEY; j++ ) {
        // NOLINTNEXTLINE(modernize-loop-convert)
        for( int i = 0; i < SEEX; i++ ) {
            const std::string this_id = ter[i][j].obj().id.str();
            if( !last_id.empty() ) {
                if( this_id == last_id ) {
                    num_same++;
                } else {
                    if( num_same == 1 ) {
                        // if there's only one element don't write as an array
                        jsout.write( last_id );
                    } else {
                        jsout.start_array();
                        jsout.write( last_id );
                        jsout.write( num_same );
                        jsout.end_array();
                        num_same = 1;
                    }
                    last_id = this_id;
                }
            } else {
                last_id = this_id;
            }
        }
    }
    // Because of the RLE scheme we have to do one last pass
    if( num_same == 1 ) {
        jsout.write( last_id );
    } else {
        jsout.start_array();
        jsout.write( last_id );
        jsout.write( num_same );
        jsout.end_array();
    }
    jsout.end_array();

    // Write out the radiation array in a simple RLE scheme.
    // written in intensity, count pairs
    jsout.member( "radiation" );
    jsout.start_array();
    int lastrad = -1;
    int count = 0;
    for( int j = 0; j < SEEY; j++ ) {
        for( int i = 0; i < SEEX; i++ ) {
            const point p( i, j );
            // Save radiation, re-examine this because it doesn't look like it works right
            int r = get_radiation( p );
            if( r == lastrad ) {
                count++;
            } else {
                if( count ) {
                    jsout.write( count );
                }
                jsout.write( r );
                lastrad = r;
                count = 1;
            }
        }
    }
    jsout.write( count );
    jsout.end_array();

    jsout.member( "furniture" );
    jsout.start_array();
    for( int j = 0; j < SEEY; j++ ) {
        for( int i = 0; i < SEEX; i++ ) {
            const point p( i, j );
            // Save furniture
            if( get_furn( p ) ) {
                jsout.start_array();
                jsout.write( p.x );
                jsout.write( p.y );
                jsout.write( get_furn( p ).obj().id );
                jsout.end_array();
            }
        }
    }
    jsout.end_array();

    jsout.member( "items" );
    jsout.start_array();
    for( int j = 0; j < SEEY; j++ ) {
        for( int i = 0; i < SEEX; i++ ) {
            if( itm[i][j].empty() ) {
                continue;
            }
            jsout.write( i );
            jsout.write( j );
            jsout.write( itm[i][j] );
        }
    }
    jsout.end_array();

    jsout.member( "traps" );
    jsout.start_array();
    for( int j = 0; j < SEEY; j++ ) {
        for( int i = 0; i < SEEX; i++ ) {
            const point p( i, j );
            // Save traps
            if( get_trap( p ) ) {
                jsout.start_array();
                jsout.write( p.x );
                jsout.write( p.y );
                // TODO: jsout should support writing an id like jsout.write( trap_id )
                jsout.write( get_trap( p ).id().str() );
                jsout.end_array();
            }
        }
    }
    jsout.end_array();

    jsout.member( "fields" );
    jsout.start_array();
    for( int j = 0; j < SEEY; j++ ) {
        for( int i = 0; i < SEEX; i++ ) {
            // Save fields
            if( fld[i][j].field_count() > 0 ) {
                jsout.write( i );
                jsout.write( j );
                jsout.start_array();
                for( auto &elem : fld[i][j] ) {
                    const field_entry &cur = elem.second;
                    jsout.write( cur.get_field_type().id() );
                    jsout.write( cur.get_field_intensity() );
                    jsout.write( cur.get_field_age() );
                }
                jsout.end_array();
            }
        }
    }
    jsout.end_array();

    // Write out as array of arrays of single entries
    jsout.member( "cosmetics" );
    jsout.start_array();
    for( const auto &cosm : cosmetics ) {
        jsout.start_array();
        jsout.write( cosm.pos.x );
        jsout.write( cosm.pos.y );
        jsout.write( cosm.type );
        jsout.write( cosm.str );
        jsout.end_array();
    }
    jsout.end_array();

    // Output the spawn points
    jsout.member( "spawns" );
    jsout.start_array();
    for( auto &elem : spawns ) {
        jsout.start_array();
        // TODO: json should know how to write string_ids
        jsout.write( elem.type.str() );
        jsout.write( elem.count );
        jsout.write( elem.pos.x );
        jsout.write( elem.pos.y );
        jsout.write( elem.faction_id );
        jsout.write( elem.mission_id );
        jsout.write( elem.is_friendly() );
        jsout.write( elem.name );
        jsout.end_array();
    }
    jsout.end_array();

    jsout.member( "vehicles" );
    jsout.start_array();
    for( auto &elem : vehicles ) {
        // json lib doesn't know how to turn a vehicle * into a vehicle,
        // so we have to iterate manually.
        jsout.write( *elem );
    }
    jsout.end_array();

    jsout.member( "partial_constructions" );
    jsout.start_array();
    for( auto &elem : partial_constructions ) {
        jsout.write( elem.first.x );
        jsout.write( elem.first.y );
        jsout.write( elem.first.z );
        jsout.write( elem.second->counter );
        jsout.write( elem.second->id.id() );
        jsout.start_array();
        for( auto &it : elem.second->components ) {
            jsout.write( it );
        }
        jsout.end_array();
    }
    jsout.end_array();

    if( legacy_computer ) {
        // it's possible that no access to computers has been made and legacy_computer
        // is not cleared
        jsout.member( "computers", *legacy_computer );
    } else if( !computers.empty() ) {
        jsout.member( "computers" );
        jsout.start_array();
        for( auto &elem : computers ) {
            jsout.write( elem.first );
            jsout.write( elem.second );
        }
        jsout.end_array();
    }

    jsout.member( "active_furniture" );
    jsout.start_array();
    for( auto &pr : active_furniture ) {
        jsout.write( pr.first );
        pr.second.serialize( jsout );
    }
    jsout.end_array();
}

void submap::load( JsonIn &jsin, const std::string &member_name, int version,
                   const tripoint offset )
{
    if( member_name == "turn_last_touched" ) {
        last_touched = calendar::turn_zero + time_duration::from_turns( jsin.get_int() );
    } else if( member_name == "temperature" ) {
        temperature = jsin.get_int();
    } else if( member_name == "terrain" ) {
        // TODO: try block around this to error out if we come up short?
        jsin.start_array();
        // terrain is encoded using simple RLE
        int remaining = 0;
        int_id<ter_t> iid;
        for( int j = 0; j < SEEY; j++ ) {
            // NOLINTNEXTLINE(modernize-loop-convert)
            for( int i = 0; i < SEEX; i++ ) {
                if( !remaining ) {
                    if( jsin.test_string() ) {
                        iid = ter_str_id( jsin.get_string() ).id();
                    } else if( jsin.test_array() ) {
                        jsin.start_array();
                        iid = ter_str_id( jsin.get_string() ).id();
                        remaining = jsin.get_int() - 1;
                        jsin.end_array();
                    } else {
                        debugmsg( "Mapbuffer terrain data is corrupt, expected string or array." );
                    }
                } else {
                    --remaining;
                }
                ter[i][j] = iid;
            }
        }
        if( remaining ) {
            debugmsg( "Mapbuffer terrain data is corrupt, tile data remaining." );
        }
        jsin.end_array();
    } else if( member_name == "radiation" ) {
        int rad_cell = 0;
        jsin.start_array();
        while( !jsin.end_array() ) {
            int rad_strength = jsin.get_int();
            int rad_num = jsin.get_int();
            for( int i = 0; i < rad_num; ++i ) {
                if( rad_cell < SEEX * SEEY ) {
                    set_radiation( { 0 % SEEX, rad_cell / SEEX }, rad_strength );
                    rad_cell++;
                }
            }
        }
    } else if( member_name == "furniture" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            frn[i][j] = furn_id( jsin.get_string() );
            jsin.end_array();
        }
    } else if( member_name == "items" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point p( i, j );
            jsin.start_array();
            while( !jsin.end_array() ) {
                detached_ptr<item> tmp;
                jsin.read( tmp );

                if( tmp->is_emissive() ) {
                    update_lum_add( p, *tmp );
                }

                if( savegame_loading_version >= 27 && version < 27 ) {
                    tmp->legacy_fast_forward_time();
                }
                item &obj = *tmp;
                itm[p.x][p.y].push_back( std::move( tmp ) );
                if( obj.needs_processing() ) {
                    active_items.add( obj );
                }
            }
        }
        for( auto &it1 : itm ) {
            for( auto &it2 : it1 ) {
                std::vector<detached_ptr<item>> cleared = it2.clear();
                to_cbc_migration::migrate( cleared );
                for( detached_ptr<item> &item : cleared ) {
                    it2.push_back( std::move( item ) );
                }
            }
        }
    } else if( member_name == "traps" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point p( i, j );
            // TODO: jsin should support returning an id like jsin.get_id<trap>()
            trp[p.x][p.y] = trap_str_id( jsin.get_string() ).id();
            jsin.end_array();
        }
    } else if( member_name == "fields" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            // Coordinates loop
            int i = jsin.get_int();
            int j = jsin.get_int();
            jsin.start_array();
            while( !jsin.end_array() ) {
                // TODO: Check enum->string migration below
                int type_int = 0;
                std::string type_str;
                if( jsin.test_int() ) {
                    type_int = jsin.get_int();
                } else {
                    type_str = jsin.get_string();
                }
                int intensity = jsin.get_int();
                int age = jsin.get_int();
                field_type_id ft;
                if( !type_str.empty() ) {
                    ft = field_type_id( type_str );
                } else {
                    ft = field_types::get_field_type_by_legacy_enum( type_int ).id;
                }
                if( fld[i][j].find_field( ft ) == nullptr ) {
                    field_count++;
                }
                fld[i][j].add_field( ft, intensity, time_duration::from_turns( age ) );
            }
        }
    } else if( member_name == "graffiti" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point p( i, j );
            set_graffiti( p, jsin.get_string() );
            jsin.end_array();
        }
    } else if( member_name == "cosmetics" ) {
        jsin.start_array();
        std::map<std::string, std::string> tcosmetics;

        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point p( i, j );
            std::string type, str;
            // Try to read as current format
            if( jsin.test_string() ) {
                type = jsin.get_string();
                str = jsin.get_string();
                insert_cosmetic( p, type, str );
            } else {
                // Otherwise read as most recent old format
                jsin.read( tcosmetics );
                for( auto &cosm : tcosmetics ) {
                    insert_cosmetic( p, cosm.first, cosm.second );
                }
                tcosmetics.clear();
            }

            jsin.end_array();
        }
    } else if( member_name == "spawns" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            // TODO: json should know how to read an string_id
            const mtype_id type = mtype_id( jsin.get_string() );
            int count = jsin.get_int();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point p( i, j );
            int faction_id = jsin.get_int();
            int mission_id = jsin.get_int();
            bool friendly = jsin.get_bool();
            std::string name = jsin.get_string();
            jsin.end_array();
            spawn_point tmp( type, count, p, faction_id, mission_id,
                             spawn_point::friendly_to_spawn_disposition( friendly ), name );
            spawns.push_back( tmp );
        }
    } else if( member_name == "vehicles" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            std::unique_ptr<vehicle> tmp = std::make_unique<vehicle>();
            jsin.read( *tmp );
            vehicles.push_back( std::move( tmp ) );
        }
    } else if( member_name == "partial_constructions" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            int i = jsin.get_int();
            int j = jsin.get_int();
            int k = jsin.get_int();
            tripoint pt = tripoint( i, j, k );
            std::unique_ptr<partial_con> pc = std::make_unique<partial_con>( offset + pt );
            pc->counter = jsin.get_int();
            if( jsin.test_int() ) {
                // Oops, int id incorrectly saved by legacy code, just load it and hope for the best
                pc->id = construction_id( jsin.get_int() );
            } else {
                pc->id = construction_str_id( jsin.get_string() ).id();
            }
            jsin.start_array();
            while( !jsin.end_array() ) {
                detached_ptr<item> tmp;
                jsin.read( tmp );
                pc->components.push_back( std::move( tmp ) );
            }
            partial_constructions[pt] = std::move( pc );
        }
    } else if( member_name == "computers" ) {
        if( jsin.test_array() ) {
            jsin.start_array();
            while( !jsin.end_array() ) {
                point loc;
                jsin.read( loc );
                auto new_comp_it = computers.emplace( loc, computer( "BUGGED_COMPUTER", -100 ) ).first;
                jsin.read( new_comp_it->second );
            }
        } else {
            // only load legacy data here, but do not update to std::map, since
            // the terrain may not have been loaded yet.
            legacy_computer = std::make_unique<computer>( "BUGGED_COMPUTER", -100 );
            jsin.read( *legacy_computer );
        }
    } else if( member_name == "active_furniture" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            point_sm_ms p;
            jsin.read( p );
            active_furniture[p].deserialize( jsin );
        }
    } else {
        jsin.skip_value();
    }
}

void advanced_inv_pane_save_state::serialize( JsonOut &json, const std::string &prefix ) const
{
    json.member( prefix + "sort_idx", sort_idx );
    json.member( prefix + "filter", filter );
    json.member( prefix + "area_idx", area_idx );
    json.member( prefix + "selected_idx", selected_idx );
    json.member( prefix + "in_vehicle", in_vehicle );
}

void advanced_inv_pane_save_state::deserialize( const JsonObject &jo,
        const std::string &prefix )
{

    jo.read( prefix + "sort_idx", sort_idx );
    jo.read( prefix + "filter", filter );
    jo.read( prefix + "area_idx", area_idx );
    jo.read( prefix + "selected_idx", selected_idx );
    jo.read( prefix + "in_vehicle", in_vehicle );
}

void advanced_inv_save_state::serialize( JsonOut &json, const std::string &prefix ) const
{
    json.member( prefix + "active_left", active_left );
    json.member( prefix + "last_popup_dest", last_popup_dest );

    json.member( prefix + "saved_area", saved_area );
    json.member( prefix + "saved_area_right", saved_area_right );
    pane.serialize( json, prefix + "pane_" );
    pane_right.serialize( json, prefix + "pane_right_" );
}

void advanced_inv_save_state::deserialize( const JsonObject &jo, const std::string &prefix )
{
    jo.read( prefix + "active_left", active_left );
    jo.read( prefix + "last_popup_dest", last_popup_dest );

    jo.read( prefix + "saved_area", saved_area );
    jo.read( prefix + "saved_area_right", saved_area_right );
    pane.area_idx = saved_area;
    pane_right.area_idx = saved_area_right;
    pane.deserialize( jo, prefix + "pane_" );
    pane_right.deserialize( jo, prefix + "pane_right_" );
}

void wisheffect_state::serialize( JsonOut &json ) const
{
    // Empty for now
    json.start_object();
    json.end_object();
}

void wisheffect_state::deserialize( const JsonObject &jo )
{
    // Empty for now
    jo.allow_omitted_members();
}

void debug_menu_state::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "effect", effect );
    json.end_object();
}

void debug_menu_state::deserialize( const JsonObject &jo )
{
    jo.read( "effect", effect );
}

void uistatedata::serialize( JsonOut &json ) const
{
    const unsigned int input_history_save_max = 25;
    json.start_object();

    transfer_save.serialize( json, "transfer_save_" );

    /**** if you want to save whatever so it's whatever when the game is started next, declare here and.... ****/
    // non array stuffs
    json.member( "adv_inv_container_location", adv_inv_container_location );
    json.member( "adv_inv_container_index", adv_inv_container_index );
    json.member( "adv_inv_container_in_vehicle", adv_inv_container_in_vehicle );
    json.member( "adv_inv_container_type", adv_inv_container_type );
    json.member( "adv_inv_container_content_type", adv_inv_container_content_type );
    json.member( "debug_menu", debug_menu );
    json.member( "editmap_nsa_viewmode", editmap_nsa_viewmode );
    json.member( "overmap_blinking", overmap_blinking );
    json.member( "overmap_show_overlays", overmap_show_overlays );
    json.member( "overmap_show_map_notes", overmap_show_map_notes );
    json.member( "overmap_show_land_use_codes", overmap_show_land_use_codes );
    json.member( "overmap_show_city_labels", overmap_show_city_labels );
    json.member( "overmap_show_hordes", overmap_show_hordes );
    json.member( "overmap_show_forest_trails", overmap_show_forest_trails );
    json.member( "overmap_highlighted_omts", overmap_highlighted_omts );
    json.member( "vmenu_show_items", vmenu_show_items );
    json.member( "list_item_sort", list_item_sort );
    json.member( "list_item_filter_active", list_item_filter_active );
    json.member( "list_item_downvote_active", list_item_downvote_active );
    json.member( "list_item_priority_active", list_item_priority_active );
    json.member( "hidden_recipes", hidden_recipes );
    json.member( "favorite_recipes", favorite_recipes );
    json.member( "recent_recipes", recent_recipes );
    json.member( "favorite_construct_recipes", favorite_construct_recipes );
    json.member( "bionic_ui_sort_mode", bionic_sort_mode );
    json.member( "overmap_debug_weather", overmap_debug_weather );
    json.member( "overmap_visible_weather", overmap_visible_weather );
    json.member( "msg_window_wide_display", msg_window_wide_display );
    json.member( "msg_window_full_height_display", msg_window_full_height_display );

    json.member( "input_history" );
    json.start_object();
    for( auto &e : input_history ) {
        json.member( e.first );
        const std::vector<std::string> &history = e.second;
        json.start_array();
        int save_start = 0;
        if( history.size() > input_history_save_max ) {
            save_start = history.size() - input_history_save_max;
        }
        for( std::vector<std::string>::const_iterator hit = history.begin() + save_start;
             hit != history.end(); ++hit ) {
            json.write( *hit );
        }
        json.end_array();
    }
    json.end_object(); // input_history

    json.end_object();
}

void uistatedata::deserialize( const JsonObject &jo )
{
    transfer_save.deserialize( jo, "transfer_save_" );

    // the rest
    jo.read( "adv_inv_container_location", adv_inv_container_location );
    jo.read( "adv_inv_container_index", adv_inv_container_index );
    jo.read( "adv_inv_container_in_vehicle", adv_inv_container_in_vehicle );
    jo.read( "adv_inv_container_type", adv_inv_container_type );
    jo.read( "adv_inv_container_content_type", adv_inv_container_content_type );
    jo.read( "debug_menu", debug_menu );
    jo.read( "editmap_nsa_viewmode", editmap_nsa_viewmode );
    jo.read( "overmap_blinking", overmap_blinking );
    jo.read( "overmap_show_overlays", overmap_show_overlays );
    jo.read( "overmap_show_map_notes", overmap_show_map_notes );
    jo.read( "overmap_show_land_use_codes", overmap_show_land_use_codes );
    jo.read( "overmap_show_city_labels", overmap_show_city_labels );
    jo.read( "overmap_show_hordes", overmap_show_hordes );
    jo.read( "overmap_show_forest_trails", overmap_show_forest_trails );
    jo.read( "overmap_highlighted_omts", overmap_highlighted_omts );
    jo.read( "hidden_recipes", hidden_recipes );
    jo.read( "favorite_recipes", favorite_recipes );
    jo.read( "recent_recipes", recent_recipes );
    jo.read( "favorite_construct_recipes", favorite_construct_recipes );
    jo.read( "bionic_ui_sort_mode", bionic_sort_mode );
    jo.read( "overmap_debug_weather", overmap_debug_weather );
    jo.read( "overmap_visible_weather", overmap_visible_weather );
    jo.read( "msg_window_wide_display", msg_window_wide_display );
    jo.read( "msg_window_full_height_display", msg_window_full_height_display );

    if( !jo.read( "vmenu_show_items", vmenu_show_items ) ) {
        // This is an old save: 1 means view items, 2 means view monsters,
        // -1 means uninitialized
        vmenu_show_items = jo.get_int( "list_item_mon", -1 ) != 2;
    }

    jo.read( "list_item_sort", list_item_sort );
    jo.read( "list_item_filter_active", list_item_filter_active );
    jo.read( "list_item_downvote_active", list_item_downvote_active );
    jo.read( "list_item_priority_active", list_item_priority_active );

    for( const JsonMember member : jo.get_object( "input_history" ) ) {
        std::vector<std::string> &v = gethistory( member.name() );
        v.clear();
        for( const std::string line : member.get_array() ) {
            v.push_back( line );
        }
    }
    // fetch list_item settings from input_history
    if( !gethistory( "item_filter" ).empty() ) {
        list_item_filter = gethistory( "item_filter" ).back();
    }
    if( !gethistory( "list_item_downvote" ).empty() ) {
        list_item_downvote = gethistory( "list_item_downvote" ).back();
    }
    if( !gethistory( "list_item_priority" ).empty() ) {
        list_item_priority = gethistory( "list_item_priority" ).back();
    }
}
