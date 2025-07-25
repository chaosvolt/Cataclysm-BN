#include "mapgen.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "advanced_inv_listitem.h"
#include "all_enum_values.h"
#include "avatar.h"
#include "calendar.h"
#include "catacharset.h"
#include "catalua.h"
#include "character_id.h"
#include "clzones.h"
#include "numeric_interval.h"
#include "computer.h"
#include "coordinate_conversions.h"
#include "coordinates.h"
#include "debug.h"
#include "drawing_primitives.h"
#include "enums.h"
#include "field_type.h"
#include "game.h"
#include "game_constants.h"
#include "generic_factory.h"
#include "input.h"
#include "int_id.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "magic_ter_furn_transform.h"
#include "map.h"
#include "map_extras.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "mapgen_functions.h"
#include "mapgendata.h"
#include "mapgenformat.h"
#include "memory_fast.h"
#include "mission.h"
#include "mod_manager.h"
#include "mongroup.h"
#include "npc.h"
#include "omdata.h"
#include "options.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "overmap_connection.h"
#include "player.h"
#include "point.h"
#include "point_float.h"
#include "rng.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "submap.h"
#include "text_snippets.h"
#include "tileray.h"
#include "to_string_id.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_group.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weighted_list.h"

static const itype_id itype_avgas( "avgas" );
static const itype_id itype_diesel( "diesel" );
static const itype_id itype_gasoline( "gasoline" );
static const itype_id itype_jp8( "jp8" );

static const mongroup_id GROUP_BLOB( "GROUP_BLOB" );
static const mongroup_id GROUP_BREATHER( "GROUP_BREATHER" );
static const mongroup_id GROUP_BREATHER_HUB( "GROUP_BREATHER_HUB" );
static const mongroup_id GROUP_DOG_THING( "GROUP_DOG_THING" );
static const mongroup_id GROUP_FUNGI_FUNGALOID( "GROUP_FUNGI_FUNGALOID" );
static const mongroup_id GROUP_HAZMATBOT( "GROUP_HAZMATBOT" );
static const mongroup_id GROUP_LAB( "GROUP_LAB" );
static const mongroup_id GROUP_LAB_CYBORG( "GROUP_LAB_CYBORG" );
static const mongroup_id GROUP_PLAIN( "GROUP_PLAIN" );
static const mongroup_id GROUP_SEWER( "GROUP_SEWER" );
static const mongroup_id GROUP_TURRET( "GROUP_TURRET" );
static const mongroup_id GROUP_ZOMBIE( "GROUP_ZOMBIE" );
static const mongroup_id GROUP_ZOMBIE_COP( "GROUP_ZOMBIE_COP" );

static const trait_id trait_NPC_STATIC_NPC( "NPC_STATIC_NPC" );

#define dbg(x) DebugLogFL((x),DC::MapGen)

static constexpr int MON_RADIUS = 3;

static void science_room( map *m, const point &p1, const point &p2, int z, int rotate );

// (x,y,z) are absolute coordinates of a submap
// x%2 and y%2 must be 0!
void map::generate( const tripoint &p, const time_point &when )
{
    dbg( DL::Info ) << "map::generate( g[" << g.get() << "], p[" << p <<
                    "], when[" << to_string( when ) << "] )";

    set_abs_sub( p );

    // First we have to create new submaps and initialize them to 0 all over
    // We create all the submaps, even if we're not a tinymap, so that map
    //  generation which overflows won't cause a crash.  At the bottom of this
    //  function, we save the upper-left 4 submaps, and delete the rest.
    // Mapgen is not z-level aware yet. Only actually initialize current z-level
    //  because other submaps won't be touched.
    for( int gridx = 0; gridx < my_MAPSIZE; gridx++ ) {
        for( int gridy = 0; gridy < my_MAPSIZE; gridy++ ) {
            const size_t grid_pos = get_nonant( { gridx, gridy, p.z } );
            if( getsubmap( grid_pos ) ) {
                debugmsg( "Submap already exists at (%d, %d, %d)", gridx, gridy, p.z );
                continue;
            }
            setsubmap( grid_pos, new submap( getabs( sm_to_ms_copy( {gridx, gridy, p.z} ) ) ) );
            // TODO: memory leak if the code below throws before the submaps get stored/deleted!
        }
    }
    // x, and y are submap coordinates, convert to overmap terrain coordinates
    // TODO: fix point types
    tripoint_abs_omt abs_omt( sm_to_omt_copy( p ) );
    oter_id terrain_type = overmap_buffer.ter( abs_omt );

    // This attempts to scale density of zombies inversely with distance from the nearest city.
    // In other words, make city centers dense and perimeters sparse.
    float density = 0.0;
    for( int i = -MON_RADIUS; i <= MON_RADIUS; i++ ) {
        for( int j = -MON_RADIUS; j <= MON_RADIUS; j++ ) {
            density += overmap_buffer.ter( abs_omt + point( i, j ) )->get_mondensity();
        }
    }
    density = density / 100;

    mapgendata dat( abs_omt, *this, density, when, nullptr );
    draw_map( dat );

    // At some point, we should add region information so we can grab the appropriate extras
    map_extras ex = region_settings_map["default"].region_extras[terrain_type->get_extras()];
    if( ex.chance > 0 && one_in( ex.chance ) ) {
        std::string *extra = ex.values.pick();
        if( extra == nullptr ) {
            debugmsg( "failed to pick extra for type %s", terrain_type->get_extras() );
        } else {
            MapExtras::apply_function( *( ex.values.pick() ), *this, abs_sub );
        }
    }

    const auto &spawns = terrain_type->get_static_spawns();

    float spawn_density = 1.0f;
    if( MonsterGroupManager::is_animal( spawns.group ) ) {
        spawn_density = get_option< float >( "SPAWN_ANIMAL_DENSITY" );
    } else {
        spawn_density = get_option< float >( "SPAWN_DENSITY" );
    }

    // Apply a multiplier to the number of monsters for really high densities.
    float odds_after_density = spawns.chance * spawn_density;
    const float max_odds = 100 - ( 100 - spawns.chance ) / 2.0;
    float density_multiplier = 1.0f;
    if( odds_after_density > max_odds ) {
        density_multiplier = 1.0f * odds_after_density / max_odds;
        odds_after_density = max_odds;
    }
    const int spawn_count = roll_remainder( density_multiplier );

    if( spawns.group && x_in_y( odds_after_density, 100 ) ) {
        int pop = spawn_count * rng( spawns.population.min, spawns.population.max );
        for( ; pop > 0; pop-- ) {
            MonsterGroupResult spawn_details = MonsterGroupManager::GetResultFromGroup( spawns.group, &pop );
            if( !spawn_details.name ) {
                continue;
            }
            if( const std::optional<tripoint> pt =
            random_point( *this, [this]( const tripoint & n ) {
            return passable( n );
            } ) ) {
                add_spawn( spawn_details.name, spawn_details.pack_size, *pt );
            }
        }
    }

    cata::run_on_mapgen_postprocess_hooks(
        *DynamicDataLoader::get_instance().lua,
        *this,
        sm_to_omt_copy( p ),
        when
    );

    // Okay, we know who are neighbors are.  Let's draw!
    // And finally save used submaps and delete the rest.
    for( int i = 0; i < my_MAPSIZE; i++ ) {
        for( int j = 0; j < my_MAPSIZE; j++ ) {
            dbg( DL::Info ) << "map::generate: submap (" << i << "," << j << ")";

            const tripoint pos( i, j, p.z );
            if( i <= 1 && j <= 1 ) {
                saven( pos );
            } else {
                const size_t grid_pos = get_nonant( pos );
                delete getsubmap( grid_pos );
                setsubmap( grid_pos, nullptr );
            }
        }
    }
}

void mapgen_function_builtin::generate( mapgendata &mgd )
{
    ( *fptr )( mgd );
}

/////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
///// mapgen_function class.
///// all sorts of ways to apply our hellish reality to a grid-o-squares

class mapgen_basic_container
{
    private:
        std::vector<std::shared_ptr<mapgen_function>> mapgens_;
        weighted_int_list<std::shared_ptr<mapgen_function>> weights_;

    public:
        int add( const std::shared_ptr<mapgen_function> &ptr ) {
            assert( ptr );
            if( std::find( mapgens_.begin(), mapgens_.end(), ptr ) != mapgens_.end() ) {
                debugmsg( "Adding duplicate mapgen to container!" );
            }
            mapgens_.push_back( ptr );
            return mapgens_.size() - 1;
        }
        /**
         * Pick a mapgen function randomly and call its generate function.
         * This basically runs the mapgen functions with the given @ref mapgendata
         * as argument.
         * @return Whether the mapgen function has been run. It may not get run if
         * the list of mapgen functions is effectively empty.
         * @p hardcoded_weight Weight for an additional entry. If that entry is chosen,
         * false is returned. If unsure, just use 0 for it.
         */
        bool generate( mapgendata &dat, const int hardcoded_weight ) const {
            if( hardcoded_weight > 0 &&
                rng( 1, weights_.get_weight() + hardcoded_weight ) > weights_.get_weight() ) {
                return false;
            }
            const std::shared_ptr<mapgen_function> *const ptr = weights_.pick();
            if( !ptr ) {
                return false;
            }
            assert( *ptr );
            ( *ptr )->generate( dat );
            return true;
        }
        /**
         * Calls @ref mapgen_function::setup and sets up the internal weighted list using
         * the **current** value of @ref mapgen_function::weight. This value may have
         * changed since it was first added, so this is needed to recalculate the weighted list.
         */
        void setup() {
            for( const std::shared_ptr<mapgen_function> &ptr : mapgens_ ) {
                const int weight = ptr->weight;
                if( weight < 1 ) {
                    continue; // rejected!
                }
                weights_.add( ptr, weight );
                ptr->setup();
            }
            // Not needed anymore, pointers are now stored in weights_ (or not used at all)
            mapgens_.clear();
        }
        void finalize_parameters() {
            for( auto &mapgen_function_ptr : weights_ ) {
                mapgen_function_ptr.obj->finalize_parameters();
            }
        }
        void check_consistency( const std::string &key ) {
            for( auto &mapgen_function_ptr : weights_ ) {
                mapgen_function_ptr.obj->check( key );
            }
        }

        mapgen_parameters get_mapgen_params( mapgen_parameter_scope scope,
                                             const std::string &context ) const {
            mapgen_parameters result;
            for( const weighted_object<int, std::shared_ptr<mapgen_function>> &p : weights_ ) {
                result.check_and_merge( p.obj->get_mapgen_params( scope ), context );
            }
            return result;
        }
};

class mapgen_factory
{
    private:
        std::map<std::string, mapgen_basic_container> mapgens_;

        /// Collect all the possible and expected keys that may get used with @ref pick.
        static std::set<std::string> get_usages() {
            std::set<std::string> result;
            for( const oter_t &elem : overmap_terrains::get_all() ) {
                result.insert( elem.get_mapgen_id() );
                result.insert( elem.id.str() );
            }
            // Why do I have to repeat the MapExtras here? Wouldn't "MapExtras::factory" be enough?
            for( const map_extra &elem : MapExtras::mapExtraFactory().get_all() ) {
                if( elem.generator_method == map_extra_method::mapgen ) {
                    result.insert( elem.generator_id );
                }
            }
            // Used in C++ code only, see calls to `oter_mapgen.generate()` below
            result.insert( "lab_1side" );
            result.insert( "lab_4side" );
            result.insert( "lab_finale_1level" );
            return result;
        }

    public:
        void reset() {
            mapgens_.clear();
        }
        /// @see mapgen_basic_container::setup
        void setup() {
            for( std::pair<const std::string, mapgen_basic_container> &omw : mapgens_ ) {
                omw.second.setup();
                inp_mngr.pump_events();
            }
            // Dummy entry, overmap terrain null should never appear and is
            // therefore never generated.
            mapgens_.erase( "null" );
        }
        void finalize_parameters() {
            for( std::pair<const std::string, mapgen_basic_container> &omw : mapgens_ ) {
                omw.second.finalize_parameters();
            }
        }
        void check_consistency() {
            // Cache all strings that may get looked up here so we don't have to go through
            // all the sources for them upon each loop.
            const std::set<std::string> usages = get_usages();
            for( std::pair<const std::string, mapgen_basic_container> &omw : mapgens_ ) {
                omw.second.check_consistency( omw.first );
                if( !usages.contains( omw.first ) ) {
                    debugmsg( "Mapgen %s is not used by anything!", omw.first );
                }
            }
        }
        /**
         * Checks whether we have an entry for the given key.
         * Note that the entry itself may not contain any valid mapgen instance
         * (could all have been removed via @ref erase).
         */
        bool has( const std::string &key ) const {
            return mapgens_.contains( key );
        }
        /// @see mapgen_basic_container::add
        int add( const std::string &key, const std::shared_ptr<mapgen_function> &ptr ) {
            return mapgens_[key].add( ptr );
        }
        /// @see mapgen_basic_container::generate
        bool generate( mapgendata &dat, const std::string &key, const int hardcoded_weight = 0 ) const {
            const auto iter = mapgens_.find( disable_mapgen ? "test" : key );
            if( iter == mapgens_.end() ) {
                return false;
            }
            return iter->second.generate( dat, hardcoded_weight );
        }

        mapgen_parameters get_map_special_params( const std::string &key ) const {
            const auto iter = mapgens_.find( key );
            if( iter == mapgens_.end() ) {
                return mapgen_parameters();
            }
            return iter->second.get_mapgen_params( mapgen_parameter_scope::overmap_special,
                                                   string_format( "map special %s", key ) );
        }
};

static mapgen_factory oter_mapgen;

/*
 * stores function ref and/or required data
 */
std::map<std::string, weighted_int_list<std::shared_ptr<mapgen_function_json_nested>> >
        nested_mapgen;
std::map<std::string, std::vector<std::unique_ptr<update_mapgen_function_json>> > update_mapgen;

/*
 * setup mapgen_basic_container::weights_ which mapgen uses to diceroll. Also setup mapgen_function_json
 */
void calculate_mapgen_weights()   // TODO: rename as it runs jsonfunction setup too
{
    oter_mapgen.setup();
    // Not really calculate weights, but let's keep it here for now
    for( auto &pr : nested_mapgen ) {
        for( weighted_object<int, std::shared_ptr<mapgen_function_json_nested>> &ptr : pr.second ) {
            ptr.obj->setup();
            inp_mngr.pump_events();
        }
    }
    for( auto &pr : update_mapgen ) {
        for( auto &ptr : pr.second ) {
            ptr->setup();
            inp_mngr.pump_events();
        }
    }
    // Having set up all the mapgens we can now perform a second
    // pass of finalizing their parameters
    oter_mapgen.finalize_parameters();
    for( auto &pr : nested_mapgen ) {
        for( weighted_object<int, std::shared_ptr<mapgen_function_json_nested>> &ptr : pr.second ) {
            ptr.obj->finalize_parameters();
            inp_mngr.pump_events();
        }
    }
    for( auto &pr : update_mapgen ) {
        for( auto &ptr : pr.second ) {
            ptr->finalize_parameters();
            inp_mngr.pump_events();
        }
    }
}

void check_mapgen_definitions()
{
    oter_mapgen.check_consistency();
    for( auto &oter_definition : nested_mapgen ) {
        for( auto &mapgen_function_ptr : oter_definition.second ) {
            mapgen_function_ptr.obj->check( oter_definition.first );
        }
    }
    for( auto &oter_definition : update_mapgen ) {
        for( auto &mapgen_function_ptr : oter_definition.second ) {
            mapgen_function_ptr->check( oter_definition.first );
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
///// json mapgen functions
///// 1 - init():

/**
 * Tiny little namespace to hold error messages
 */
namespace mapgen_defer
{
std::string member;
std::string message;
bool defer;
JsonObject jsi;
} // namespace mapgen_defer

static void set_mapgen_defer( const JsonObject &jsi, const std::string &member,
                              const std::string &message )
{
    mapgen_defer::defer = true;
    mapgen_defer::jsi = jsi;
    mapgen_defer::member = member;
    mapgen_defer::message = message;
}

/*
 * load a single mapgen json structure; this can be inside an overmap_terrain, or on it's own.
 */
std::shared_ptr<mapgen_function>
load_mapgen_function( const JsonObject &jio, point offset, point total )
{
    int mgweight = jio.get_int( "weight", 1000 );
    if( mgweight <= 0 || jio.get_bool( "disabled", false ) ) {
        jio.allow_omitted_members();
        return nullptr; // nothing
    }
    const std::string mgtype = jio.get_string( "method" );
    if( mgtype == "builtin" ) {
        if( const building_gen_pointer ptr = get_mapgen_cfunction( jio.get_string( "name" ) ) ) {
            return std::make_shared<mapgen_function_builtin>( ptr, mgweight );
        } else {
            jio.throw_error( "function does not exist", "name" );
        }
    } else if( mgtype == "json" ) {
        if( !jio.has_object( "object" ) ) {
            jio.throw_error( R"(mapgen with method "json" must define key "object")" );
        }
        JsonObject jo = jio.get_object( "object" );
        const json_source_location jsrc = jo.get_source_location();
        jo.allow_omitted_members();
        return std::make_shared<mapgen_function_json>(
                   jsrc, mgweight, offset, total );
    } else {
        jio.throw_error( R"(invalid value: must be "builtin" or "json")", "method" );
    }
}

void load_and_add_mapgen_function( const JsonObject &jio, const std::string &id_base,
                                   point offset, point total )
{
    std::shared_ptr<mapgen_function> f = load_mapgen_function( jio, offset, total );
    if( f ) {
        oter_mapgen.add( id_base, f );
    }
}

static void load_nested_mapgen( const JsonObject &jio, const std::string &id_base )
{
    const std::string mgtype = jio.get_string( "method" );
    if( mgtype == "json" ) {
        if( jio.has_object( "object" ) ) {
            int weight = jio.get_int( "weight", 1000 );
            JsonObject jo = jio.get_object( "object" );
            const json_source_location jsrc = jo.get_source_location();
            jo.allow_omitted_members();
            nested_mapgen[id_base].add( std::make_shared<mapgen_function_json_nested>( jsrc ), weight );
        } else {
            debugmsg( "Nested mapgen: Invalid mapgen function (missing \"object\" object)", id_base.c_str() );
        }
    } else {
        debugmsg( "Nested mapgen: type for id %s was %s, but nested mapgen only supports \"json\"",
                  id_base.c_str(), mgtype.c_str() );
    }
}

static void load_update_mapgen( const JsonObject &jio, const std::string &id_base )
{
    const std::string mgtype = jio.get_string( "method" );
    if( mgtype == "json" ) {
        if( jio.has_object( "object" ) ) {
            JsonObject jo = jio.get_object( "object" );
            const json_source_location jsrc = jo.get_source_location();
            jo.allow_omitted_members();
            update_mapgen[id_base].push_back(
                std::make_unique<update_mapgen_function_json>( jsrc ) );
        } else {
            debugmsg( "Update mapgen: Invalid mapgen function (missing \"object\" object)",
                      id_base.c_str() );
        }
    } else {
        debugmsg( "Update mapgen: type for id %s was %s, but update mapgen only supports \"json\"",
                  id_base.c_str(), mgtype.c_str() );
    }
}

/*
 * feed bits `o json from standalone file to load_mapgen_function. (standalone json "type": "mapgen")
 */
void load_mapgen( const JsonObject &jo )
{
    // NOLINTNEXTLINE(cata-use-named-point-constants)
    static constexpr point point_one( 1, 1 );

    if( jo.has_array( "om_terrain" ) ) {
        JsonArray ja = jo.get_array( "om_terrain" );
        if( ja.test_array() ) {
            point offset;
            point total( ja.get_array( 0 ).size(), ja.size() );
            for( JsonArray row_items : ja ) {
                for( const std::string mapgenid : row_items ) {
                    load_and_add_mapgen_function( jo, mapgenid, offset, total );
                    offset.x++;
                }
                offset.y++;
                offset.x = 0;
            }
        } else {
            std::vector<std::string> mapgenid_list;
            for( const std::string line : ja ) {
                mapgenid_list.push_back( line );
            }
            if( !mapgenid_list.empty() ) {
                const auto mgfunc = load_mapgen_function( jo, point_zero, point_one );
                if( mgfunc ) {
                    for( auto &i : mapgenid_list ) {
                        oter_mapgen.add( i, mgfunc );
                    }
                }
            }
        }
    } else if( jo.has_string( "om_terrain" ) ) {
        load_and_add_mapgen_function( jo, jo.get_string( "om_terrain" ), point_zero, point_one );
    } else if( jo.has_string( "nested_mapgen_id" ) ) {
        load_nested_mapgen( jo, jo.get_string( "nested_mapgen_id" ) );
    } else if( jo.has_string( "update_mapgen_id" ) ) {
        load_update_mapgen( jo, jo.get_string( "update_mapgen_id" ) );
    } else {
        debugmsg( "mapgen entry requires \"om_terrain\" or \"nested_mapgen_id\"(string, array of strings, or array of array of strings)\n%s\n",
                  jo.str() );
    }
}

void reset_mapgens()
{
    oter_mapgen.reset();
    nested_mapgen.clear();
    update_mapgen.clear();
}

/////////////////////////////////////////////////////////////////////////////////
///// 2 - right after init() finishes parsing all game json and terrain info/etc is set..
/////   ...parse more json! (mapgen_function_json)

size_t mapgen_function_json_base::calc_index( const point &p ) const
{
    if( p.x >= mapgensize.x ) {
        debugmsg( "invalid value %zu for x in calc_index", p.x );
    }
    if( p.y >= mapgensize.y ) {
        debugmsg( "invalid value %zu for y in calc_index", p.y );
    }
    return p.y * mapgensize.y + p.x;
}

static bool common_check_bounds( const jmapgen_int &x, const jmapgen_int &y,
                                 point mapgensize, const JsonObject &jso )
{
    half_open_rectangle<point> bounds( point_zero, mapgensize );
    if( !bounds.contains( point( x.val, y.val ) ) ) {
        return false;
    }

    if( x.valmax > mapgensize.x - 1 ) {
        jso.throw_error( "coordinate range cannot cross grid boundaries", "x" );
        return false;
    }

    if( y.valmax > mapgensize.y - 1 ) {
        jso.throw_error( "coordinate range cannot cross grid boundaries", "y" );
        return false;
    }

    return true;
}

void mapgen_function_json_base::merge_non_nest_parameters_into(
    mapgen_parameters &params, const std::string &outer_context ) const
{
    params.check_and_merge( parameters, outer_context, mapgen_parameter_scope::nest );
}

bool mapgen_function_json_base::check_inbounds( const jmapgen_int &x, const jmapgen_int &y,
        const JsonObject &jso ) const
{
    return common_check_bounds( x, y, mapgensize, jso );
}

mapgen_function_json_base::mapgen_function_json_base( const json_source_location &jsrcloc )
    : jsrcloc( jsrcloc )
    , is_ready( false )
    , mapgensize( SEEX * 2, SEEY * 2 )
    , total_size( mapgensize )
    , objects( m_offset, mapgensize, total_size )
{
}

mapgen_function_json_base::~mapgen_function_json_base() = default;

mapgen_function_json::mapgen_function_json( const json_source_location &jsrcloc, const int w,
        point grid_offset, point grid_total )
    : mapgen_function( w )
    , mapgen_function_json_base( jsrcloc )
    , fill_ter( t_null )
    , rotation( 0 )
{
    m_offset.x = grid_offset.x * mapgensize.x;
    m_offset.y = grid_offset.y * mapgensize.y;
    total_size.x = grid_total.x * mapgensize.x;
    total_size.y = grid_total.y * mapgensize.y;
    objects = jmapgen_objects( m_offset, mapgensize, total_size );
}

mapgen_function_json_nested::mapgen_function_json_nested( const json_source_location &jsrcloc )
    : mapgen_function_json_base( jsrcloc )
    , rotation( 0 )
{
}

jmapgen_int::jmapgen_int( point p ) : val( p.x ), valmax( p.y ) {}

jmapgen_int::jmapgen_int( const JsonObject &jo, const std::string &tag )
{
    if( jo.has_array( tag ) ) {
        JsonArray sparray = jo.get_array( tag );
        if( sparray.size() < 1 || sparray.size() > 2 ) {
            jo.throw_error( "invalid data: must be an array of 1 or 2 values", tag );
        }
        val = sparray.get_int( 0 );
        if( sparray.size() == 2 ) {
            valmax = sparray.get_int( 1 );
        } else {
            valmax = val;
        }
    } else {
        val = valmax = jo.get_int( tag );
    }
}

jmapgen_int::jmapgen_int( const JsonObject &jo, const std::string &tag, int def_val,
                          int def_valmax )
    : val( def_val )
    , valmax( def_valmax )
{
    if( jo.has_array( tag ) ) {
        JsonArray sparray = jo.get_array( tag );
        if( sparray.size() > 2 ) {
            jo.throw_error( "invalid data: must be an array of 1 or 2 values", tag );
        }
        if( sparray.size() >= 1 ) {
            val = sparray.get_int( 0 );
        }
        if( sparray.size() >= 2 ) {
            valmax = sparray.get_int( 1 );
        }
    } else if( jo.has_member( tag ) ) {
        val = valmax = jo.get_int( tag );
    }
}

int jmapgen_int::get() const
{
    return val == valmax ? val : rng( val, valmax );
}

/*
 * Turn json gobbldigook into machine friendly gobbldigook, for applying
 * basic map 'set' functions, optionally based on one_in(chance) or repeat value
 */
void mapgen_function_json_base::setup_setmap( const JsonArray &parray )
{
    std::string tmpval;
    std::map<std::string, jmapgen_setmap_op> setmap_opmap;
    setmap_opmap[ "terrain" ] = JMAPGEN_SETMAP_TER;
    setmap_opmap[ "furniture" ] = JMAPGEN_SETMAP_FURN;
    setmap_opmap[ "trap" ] = JMAPGEN_SETMAP_TRAP;
    setmap_opmap[ "radiation" ] = JMAPGEN_SETMAP_RADIATION;
    setmap_opmap[ "bash" ] = JMAPGEN_SETMAP_BASH;
    std::map<std::string, jmapgen_setmap_op>::iterator sm_it;
    jmapgen_setmap_op tmpop;
    int setmap_optype = 0;

    for( const JsonObject pjo : parray ) {
        if( pjo.read( "point", tmpval ) ) {
            setmap_optype = JMAPGEN_SETMAP_OPTYPE_POINT;
        } else if( pjo.read( "set", tmpval ) ) {
            setmap_optype = JMAPGEN_SETMAP_OPTYPE_POINT;
            debugmsg( "Warning, set: [ { \"set\": … } is deprecated, use set: [ { \"point\": … " );
        } else if( pjo.read( "line", tmpval ) ) {
            setmap_optype = JMAPGEN_SETMAP_OPTYPE_LINE;
        } else if( pjo.read( "square", tmpval ) ) {
            setmap_optype = JMAPGEN_SETMAP_OPTYPE_SQUARE;
        } else {
            pjo.throw_error( R"(invalid data: must contain "point", "set", "line" or "square" member)" );
        }

        sm_it = setmap_opmap.find( tmpval );
        if( sm_it == setmap_opmap.end() ) {
            pjo.throw_error( string_format( "invalid subfunction %s", tmpval.c_str() ) );
        }

        tmpop = sm_it->second;
        jmapgen_int tmp_x2( 0, 0 );
        jmapgen_int tmp_y2( 0, 0 );
        jmapgen_int tmp_i( 0, 0 );
        int tmp_chance = 1;
        int tmp_rotation = 0;
        int tmp_fuel = -1;
        int tmp_status = -1;

        const jmapgen_int tmp_x( pjo, "x" );
        const jmapgen_int tmp_y( pjo, "y" );
        if( !check_inbounds( tmp_x, tmp_y, pjo ) ) {
            pjo.allow_omitted_members();
            continue;
        }
        if( setmap_optype != JMAPGEN_SETMAP_OPTYPE_POINT ) {
            tmp_x2 = jmapgen_int( pjo, "x2" );
            tmp_y2 = jmapgen_int( pjo, "y2" );
            if( !check_inbounds( tmp_x2, tmp_y2, pjo ) ) {
                pjo.allow_omitted_members();
                continue;
            }
        }
        if( tmpop == JMAPGEN_SETMAP_RADIATION ) {
            tmp_i = jmapgen_int( pjo, "amount" );
        } else if( tmpop == JMAPGEN_SETMAP_BASH ) {
            //suppress warning
        } else {
            std::string tmpid = pjo.get_string( "id" );
            switch( tmpop ) {
                case JMAPGEN_SETMAP_TER: {
                    const ter_str_id tid( tmpid );

                    if( !tid.is_valid() ) {
                        set_mapgen_defer( pjo, "id", "no such terrain" );
                        return;
                    }
                    tmp_i.val = tid.id().to_i();
                }
                break;
                case JMAPGEN_SETMAP_FURN: {
                    const furn_str_id fid( tmpid );

                    if( !fid.is_valid() ) {
                        set_mapgen_defer( pjo, "id", "no such furniture" );
                        return;
                    }
                    tmp_i.val = fid.id().to_i();
                }
                break;
                case JMAPGEN_SETMAP_TRAP: {
                    const trap_str_id sid( tmpid );
                    if( !sid.is_valid() ) {
                        set_mapgen_defer( pjo, "id", "no such trap" );
                        return;
                    }
                    tmp_i.val = sid.id().to_i();
                }
                break;

                default:
                    //Suppress warnings
                    break;
            }
            // TODO: ... support for random furniture? or not.
            tmp_i.valmax = tmp_i.val;
        }
        // TODO: sanity check?
        const jmapgen_int tmp_repeat = jmapgen_int( pjo, "repeat", 1, 1 );
        pjo.read( "chance", tmp_chance );
        pjo.read( "rotation", tmp_rotation );
        pjo.read( "fuel", tmp_fuel );
        pjo.read( "status", tmp_status );
        jmapgen_setmap tmp( tmp_x, tmp_y, tmp_x2, tmp_y2,
                            static_cast<jmapgen_setmap_op>( tmpop + setmap_optype ), tmp_i,
                            tmp_chance, tmp_repeat, tmp_rotation, tmp_fuel, tmp_status );

        setmap_points.push_back( tmp );
        tmpval.clear();
    }

}

void mapgen_function_json_base::finalize_parameters_common()
{
    objects.merge_parameters_into( parameters, "" );
}

mapgen_arguments mapgen_function_json_base::get_args(
    const mapgendata &md, mapgen_parameter_scope scope ) const
{
    return parameters.get_args( md, scope );
}

jmapgen_place::jmapgen_place( const JsonObject &jsi )
    : x( jsi, "x" )
    , y( jsi, "y" )
    , repeat( jsi, "repeat", 1, 1 )
{
}

void jmapgen_place::offset( point offset )
{
    x.val -= offset.x;
    x.valmax -= offset.x;
    y.val -= offset.y;
    y.valmax -= offset.y;
}

map_key::map_key( const std::string &s ) : str( s )
{
    if( utf8_width( str ) != 1 ) {
        debugmsg( "map key '%s' must be 1 column", str );
    }
}

map_key::map_key( const JsonMember &member ) : str( member.name() )
{
    if( utf8_width( str ) != 1 ) {
        member.throw_error( "format map key must be 1 column" );
    }
}

template<typename T>
static bool is_null_helper( const string_id<T> &id )
{
    return id.is_null();
}

template<typename T>
static bool is_null_helper( const int_id<T> &id )
{
    return id.id().is_null();
}

static bool is_null_helper( const std::string & )
{
    return false;
}

template<typename T>
struct make_null_helper;

template<>
struct make_null_helper<std::string> {
    std::string operator()() const {
        return {};
    }
};

template<typename T>
struct make_null_helper<string_id<T>> {
    string_id<T> operator()() const {
        return string_id<T>::NULL_ID();
    }
};

template<typename T>
struct make_null_helper<int_id<T>> {
    int_id<T> operator()() const {
        return string_id<T>::NULL_ID().id();
    }
};

template<typename T>
static string_id<T> to_string_id_helper( const string_id<T> &id )
{
    return id;
}

template<typename T>
static string_id<T> to_string_id_helper( const int_id<T> &id )
{
    return id.id();
}

static std::string to_string_id_helper( const std::string &s )
{
    return s;
}

template<typename T>
static bool is_valid_helper( const string_id<T> &id )
{
    return id.is_valid();
}

template<typename T>
static bool is_valid_helper( const int_id<T> & )
{
    return true;
}

static bool is_valid_helper( const std::string & )
{
    return true;
}

// Mapgen often uses various id values.  Usually these are specified verbatim
// as strings, but they can also be parameterized.  This class encapsulates
// such a value.  It records how the value was specified so that it can be
// calculated later based on the parameters chosen for a particular instance of
// the mapgen.
template<typename Id>
class mapgen_value
{
    public:
        using StringId = to_string_id_t<Id>;
        struct void_;
        using Id_unless_string =
            std::conditional_t<std::is_same_v<Id, std::string>, void_, Id>;

        struct value_source {
            virtual ~value_source() = default;
            virtual Id get( const mapgendata & ) const = 0;
            virtual void check( const std::string &/*oter_name*/, const mapgen_parameters &
                              ) const {};
            virtual void check_consistent_with(
                const value_source &, const std::string &context ) const = 0;
            virtual std::vector<StringId> all_possible_results(
                const mapgen_parameters & ) const = 0;
        };

        struct null_source : value_source {
            Id get( const mapgendata & ) const override {
                return make_null_helper<Id> {}();
            }

            void check_consistent_with(
                const value_source &o, const std::string &context ) const override {
                if( const null_source *other = dynamic_cast<const null_source *>( &o ) ) {
                    // OK
                } else {
                    debugmsg( "inconsistent default types for %s", context );
                }
            }

            std::vector<StringId> all_possible_results( const mapgen_parameters & ) const override {
                return { make_null_helper<StringId>{}() };
            }
        };

        struct id_source : value_source {
            Id id;

            explicit id_source( const std::string &s ) :
                id( s ) {
            }

            explicit id_source( const Id_unless_string &s ) :
                id( s ) {
            }

            Id get( const mapgendata & ) const override {
                return id;
            }

            void check( const std::string &context, const mapgen_parameters & ) const override {
                if( !is_valid_helper( id ) ) {
                    debugmsg( "mapgen '%s' uses invalid entry '%s' in weighted list",
                              context, cata_variant( id ).get_string() );
                }
            }

            void check_consistent_with(
                const value_source &o, const std::string &context ) const override {
                if( const id_source *other = dynamic_cast<const id_source *>( &o ) ) {
                    if( id != other->id ) {
                        debugmsg( "inconsistent default values for %s (%s vs %s)",
                                  context, cata_variant( id ).get_string(),
                                  cata_variant( other->id ).get_string() );
                    }
                } else {
                    debugmsg( "inconsistent default types for %s", context );
                }
            }

            std::vector<StringId> all_possible_results( const mapgen_parameters & ) const override {
                return { to_string_id_helper( id ) };
            }
        };

        struct param_source : value_source {
            std::string param_name;
            std::optional<StringId> fallback;

            explicit param_source( const JsonObject &jo )
                : param_name( jo.get_string( "param" ) ) {
                jo.read( "fallback", fallback, false );
            }

            Id get( const mapgendata &dat ) const override {
                if( fallback ) {
                    return Id( dat.get_arg_or<StringId>( param_name, *fallback ) );
                } else {
                    return Id( dat.get_arg<StringId>( param_name ) );
                }
            }

            void check( const std::string &context, const mapgen_parameters &parameters
                      ) const override {
                auto param_it = parameters.map.find( param_name );
                if( param_it == parameters.map.end() ) {
                    debugmsg( "mapgen '%s' uses undefined parameter '%s'", context, param_name );
                } else {
                    const mapgen_parameter &param = param_it->second;
                    constexpr cata_variant_type req_type = cata_variant_type_for<StringId>();
                    cata_variant_type param_type = param.type();
                    if( param_type != req_type && req_type != cata_variant_type::string ) {
                        debugmsg( "mapgen '%s' uses parameter '%s' of type '%s' in a context "
                                  "expecting type '%s'", context, param_name,
                                  io::enum_to_string( param_type ),
                                  io::enum_to_string( req_type ) );
                    }
                    if( param.scope() == mapgen_parameter_scope::overmap_special && !fallback ) {
                        debugmsg( "mapgen '%s' uses parameter '%s' of map_special scope without a "
                                  "fallback.  Such parameters must provide a fallback to allow "
                                  "for changes to overmap_special definitions", context,
                                  param_name );
                    }
                }
            }

            void check_consistent_with(
                const value_source &o, const std::string &context ) const override {
                if( const param_source *other = dynamic_cast<const param_source *>( &o ) ) {
                    if( param_name != other->param_name ) {
                        debugmsg( "inconsistent default values for %s (%s vs %s)",
                                  context, param_name, other->param_name );
                    }
                } else {
                    debugmsg( "inconsistent default types for %s", context );
                }
            }

            std::vector<StringId> all_possible_results(
                const mapgen_parameters &params ) const override {
                auto param_it = params.map.find( param_name );
                if( param_it == params.map.end() ) {
                    return {};
                } else {
                    const mapgen_parameter &param = param_it->second;
                    std::vector<StringId> result;
                    for( const std::string &s : param.all_possible_values( params ) ) {
                        result.emplace_back( s );
                    }
                    return result;
                }
            }
        };

        struct distribution_source : value_source {
            weighted_int_list<StringId> list;

            explicit distribution_source( const JsonObject &jo ) {
                load_weighted_list( jo.get_member( "distribution" ), list, 1 );
            }

            Id get( const mapgendata & ) const override {
                return *list.pick();
            }

            void check( const std::string &context, const mapgen_parameters & ) const override {
                for( const weighted_object<int, StringId> &wo : list ) {
                    if( !is_valid_helper( wo.obj ) ) {
                        debugmsg( "mapgen '%s' uses invalid entry '%s' in weighted list",
                                  context, cata_variant( wo.obj ).get_string() );
                    }
                }
            }

            void check_consistent_with(
                const value_source &o, const std::string &context ) const override {
                if( const distribution_source *other =
                        dynamic_cast<const distribution_source *>( &o ) ) {
                    if( list != other->list ) {
                        const std::string my_list = list.to_debug_string();
                        const std::string other_list = other->list.to_debug_string();
                        debugmsg( "inconsistent default value distributions for %s (%s vs %s)",
                                  context, my_list, other_list );
                    }
                } else {
                    debugmsg( "inconsistent default types for %s", context );
                }
            }

            std::vector<StringId> all_possible_results( const mapgen_parameters & ) const override {
                std::vector<StringId> result;
                for( const weighted_object<int, StringId> &wo : list ) {
                    result.push_back( wo.obj );
                }
                return result;
            }
        };

        struct switch_source : value_source {
            // This has to be a pointer because mapgen_value is an incomplete
            // type.  We could resolve this by pulling out all these
            // value_source classes and defining them at namespace scope after
            // mapgen_value, but that would make the code much more verbose.
            std::unique_ptr<mapgen_value<std::string>> on;
            std::unordered_map<std::string, StringId> cases;

            explicit switch_source( const JsonObject &jo )
                : on( std::make_unique<mapgen_value<std::string>>( jo.get_object( "switch" ) ) ) {
                jo.read( "cases", cases, true );
            }

            Id get( const mapgendata &dat ) const override {
                std::string based_on = on->get( dat );
                auto it = cases.find( based_on );
                if( it == cases.end() ) {
                    debugmsg( "switch does not handle case %s", based_on );
                    return make_null_helper<Id> {}();
                }
                return Id( it->second );
            }

            void check( const std::string &context, const mapgen_parameters &params
                      ) const override {
                on->check( context, params );
                for( const std::pair<const std::string, StringId> &p : cases ) {
                    if( !is_valid_helper( p.second ) ) {
                        debugmsg( "mapgen '%s' uses invalid entry '%s' in switch",
                                  context, cata_variant( p.second ).get_string() );
                    }
                }
                std::vector<std::string> possible_values = on->all_possible_results( params );
                for( const std::string &value : possible_values ) {
                    if( !cases.contains( value ) ) {
                        debugmsg( "mapgen '%s' has switch whcih does not account for potential "
                                  "case '%s' of the switched-on value", context, value );
                    }
                }
            }

            void check_consistent_with(
                const value_source &o, const std::string &context ) const override {
                if( const switch_source *other = dynamic_cast<const switch_source *>( &o ) ) {
                    on->check_consistent_with( *other->on, context );
                    if( cases != other->cases ) {
                        auto dump_set = []( const std::unordered_map<std::string, StringId> &s ) {
                            bool first = true;
                            std::string result = "{ ";
                            for( const std::pair<const std::string, StringId> &p : s ) {
                                if( first ) {
                                    first = false;
                                } else {
                                    result += ", ";
                                }
                                result += p.first;
                                result += ": ";
                                result += cata_variant( p.second ).get_string();
                            }
                            return result;
                        };

                        const std::string my_list = dump_set( cases );
                        const std::string other_list = dump_set( other->cases );
                        debugmsg( "inconsistent switch cases for %s (%s vs %s)",
                                  context, my_list, other_list );
                    }
                } else {
                    debugmsg( "inconsistent default types for %s", context );
                }
            }

            std::vector<StringId> all_possible_results( const mapgen_parameters & ) const override {
                std::vector<StringId> result;
                for( const std::pair<const std::string, StringId> &p : cases ) {
                    result.push_back( p.second );
                }
                return result;
            }
        };

        mapgen_value()
            : is_null_( true )
            , source_( make_shared_fast<null_source>() )
        {}

        explicit mapgen_value( const std::string &s ) {
            init_string( s );
        }

        explicit mapgen_value( const Id_unless_string &id ) {
            init_string( id );
        }

        explicit mapgen_value( const JsonValue &jv ) {
            if( jv.test_string() ) {
                init_string( jv.get_string() );
            } else {
                init_object( jv.get_object() );
            }
        }

        explicit mapgen_value( const JsonObject &jo ) {
            init_object( jo );
        }

        template<typename S>
        void init_string( const S &s ) {
            source_ = make_shared_fast<id_source>( s );
            is_null_ = is_null_helper( s );
        }

        void init_object( const JsonObject &jo ) {
            if( jo.has_member( "param" ) ) {
                source_ = make_shared_fast<param_source>( jo );
            } else if( jo.has_member( "distribution" ) ) {
                source_ = make_shared_fast<distribution_source>( jo );
            } else if( jo.has_member( "switch" ) ) {
                source_ = make_shared_fast<switch_source>( jo );
            } else {
                jo.throw_error(
                    R"(Expected member "param", "distribution", or "switch" in mapgen object)" );
            }
        }

        bool is_null() const {
            return is_null_;
        }

        void check( const std::string &context, const mapgen_parameters &params ) const {
            source_->check( context, params );
        }
        void check_consistent_with( const mapgen_value &other, const std::string &context ) const {
            source_->check_consistent_with( *other.source_, context );
        }

        Id get( const mapgendata &dat ) const {
            return source_->get( dat );
        }
        std::vector<StringId> all_possible_results( const mapgen_parameters &params ) const {
            return source_->all_possible_results( params );
        }

        void deserialize( JsonIn &jsin ) {
            if( jsin.test_object() ) {
                *this = mapgen_value( jsin.get_object() );
            } else {
                *this = mapgen_value( jsin.get_string() );
            }
        }
    private:
        bool is_null_ = false;
        shared_ptr_fast<const value_source> source_;
};

namespace io
{

template<>
std::string enum_to_string<mapgen_parameter_scope>( mapgen_parameter_scope v )
{
    switch( v ) {
        // *INDENT-OFF*
        case mapgen_parameter_scope::overmap_special: return "overmap_special";
        case mapgen_parameter_scope::omt: return "omt";
        case mapgen_parameter_scope::nest: return "nest";
        // *INDENT-ON*
        case mapgen_parameter_scope::last:
            break;
    }
    debugmsg( "unknown debug_menu::debug_menu_index %d", static_cast<int>( v ) );
    return "";
}

} // namespace io

mapgen_parameter::mapgen_parameter() = default;

mapgen_parameter::mapgen_parameter( const mapgen_value<std::string> &def, cata_variant_type type,
                                    mapgen_parameter_scope scope )
    : scope_( scope )
    , type_( type )
    , default_( make_shared_fast<mapgen_value<std::string>>( def ) )
{}

void mapgen_parameter::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    optional( jo, false, "scope", scope_, mapgen_parameter_scope::overmap_special );
    jo.read( "type", type_, true );
    default_ = make_shared_fast<mapgen_value<std::string>>( jo.get_member( "default" ) );
}

cata_variant_type mapgen_parameter::type() const
{
    return type_;
}

cata_variant mapgen_parameter::get( const mapgendata &md ) const
{
    return cata_variant::from_string( type_, default_->get( md ) );
}

std::vector<std::string> mapgen_parameter::all_possible_values(
    const mapgen_parameters &params ) const
{
    return default_->all_possible_results( params );
}

void mapgen_parameter::check( const mapgen_parameters &params, const std::string &context ) const
{
    default_->check( context, params );
    for( const std::string &value : all_possible_values( params ) ) {
        if( !cata_variant::from_string( type_, std::string( value ) ).is_valid() ) {
            debugmsg( "%s can take value %s which is not a valid value of type %s",
                      context, value, io::enum_to_string( type_ ) );
        }
    }
}

void mapgen_parameter::check_consistent_with(
    const mapgen_parameter &other, const std::string &context ) const
{
    if( scope_ != other.scope_ ) {
        debugmsg( "mismatched scope for mapgen parameters %s (%s vs %s)",
                  context, io::enum_to_string( scope_ ), io::enum_to_string( other.scope_ ) );
    }
    if( type_ != other.type_ ) {
        debugmsg( "mismatched type for mapgen parameters %s (%s vs %s)",
                  context, io::enum_to_string( type_ ), io::enum_to_string( other.type_ ) );
    }
    default_->check_consistent_with( *other.default_, context );
}

auto mapgen_parameters::add_unique_parameter(
    const std::string &prefix, const mapgen_value<std::string> &def, cata_variant_type type,
    mapgen_parameter_scope scope ) -> iterator
{
    uint64_t i = 0;
    std::string candidate_name;
    while( true ) {
        candidate_name = string_format( "%s%d", prefix, i );
        if( map.find( candidate_name ) == map.end() ) {
            break;
        }
        ++i;
    }

    return map.emplace( candidate_name, mapgen_parameter( def, type, scope ) ).first;
}

mapgen_parameters mapgen_parameters::params_for_scope( mapgen_parameter_scope scope ) const
{
    mapgen_parameters result;
    for( const std::pair<const std::string, mapgen_parameter> &p : map ) {
        const mapgen_parameter &param = p.second;
        if( param.scope() == scope ) {
            result.map.insert( p );
        }
    }
    return result;
}

mapgen_arguments mapgen_parameters::get_args(
    const mapgendata &md, mapgen_parameter_scope scope ) const
{
    std::unordered_map<std::string, cata_variant> result;
    for( const std::pair<const std::string, mapgen_parameter> &p : map ) {
        const mapgen_parameter &param = p.second;
        if( param.scope() == scope ) {
            result.emplace( p.first, param.get( md ) );
        }
    }
    return { std::move( result ) };
}

void mapgen_parameters::check_and_merge( const mapgen_parameters &other,
        const std::string &context, mapgen_parameter_scope up_to_scope )
{
    for( const std::pair<const std::string, mapgen_parameter> &p : other.map ) {
        const mapgen_parameter &other_param = p.second;
        if( other_param.scope() >= up_to_scope ) {
            continue;
        }
        auto insert_result = map.insert( p );
        if( !insert_result.second ) {
            const std::string &name = p.first;
            const mapgen_parameter &this_param = insert_result.first->second;
            this_param.check_consistent_with(
                other_param, string_format( "parameter %s in %s", name, context ) );
        }
    }
}

/**
 * This is a generic mapgen piece, the template parameter PieceType should be another specific
 * type of jmapgen_piece. This class contains a vector of those objects and will chose one of
 * it at random.
 */
template<typename PieceType>
class jmapgen_alternatively : public jmapgen_piece
{
    public:
        // Note: this bypasses virtual function system, all items in this vector are of type
        // PieceType, they *can not* be of any other type.
        std::vector<PieceType> alternatives;
        jmapgen_alternatively() = default;
        mapgen_phase phase() const override {
            if( alternatives.empty() ) {
                return mapgen_phase::default_;
            }
            return alternatives[0].phase();
        }
        void check( const std::string &context, const mapgen_parameters &params ) const override {
            if( alternatives.empty() ) {
                debugmsg( "zero alternatives in jmapgen_alternatively in %s", context );
            }
            for( const PieceType &piece : alternatives ) {
                piece.check( context, params );
            }
        }
        void merge_parameters_into( mapgen_parameters &params,
                                    const std::string &outer_context ) const override {
            for( const PieceType &piece : alternatives ) {
                piece.merge_parameters_into( params, outer_context );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            if( const auto chosen = random_entry_opt( alternatives ) ) {
                chosen->get().apply( dat, x, y );
            }
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }
};

template<typename Value>
class jmapgen_constrained : public jmapgen_piece
{
    public:
        jmapgen_constrained( shared_ptr_fast<const jmapgen_piece> und,
                             const std::vector<mapgen_constraint<Value>> &cons )
            : underlying_piece( std::move( und ) )
            , constraints( cons )
        {}

        shared_ptr_fast<const jmapgen_piece> underlying_piece;
        std::vector<mapgen_constraint<Value>> constraints;

        mapgen_phase phase() const override {
            return underlying_piece->phase();
        }
        void check( const std::string &context, const mapgen_parameters &params ) const override {
            underlying_piece->check( context, params );
        }

        void merge_parameters_into( mapgen_parameters &params, const std::string &outer_context
                                  ) const override {
            underlying_piece->merge_parameters_into( params, outer_context );
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            for( const mapgen_constraint<Value> &constraint : constraints ) {
                Value param_value = dat.get_arg<Value>( constraint.parameter_name );
                if( param_value != constraint.value ) {
                    return;
                }
            }
            underlying_piece->apply( dat, x, y );
        }
};

/**
 * Places fields on the map.
 * "field": field type ident.
 * "intensity": initial field intensity.
 * "age": initial field age.
 */
class jmapgen_field : public jmapgen_piece
{
    public:
        mapgen_value<field_type_id> ftype;
        int intensity;
        time_duration age;
        jmapgen_field( const JsonObject &jsi ) :
            ftype( jsi.get_member( "field" ) )
            , intensity( jsi.get_int( "intensity", 1 ) )
            , age( time_duration::from_turns( jsi.get_int( "age", 0 ) ) ) {
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            field_type_id chosen_id = ftype.get( dat );
            if( chosen_id.id().is_null() ) {
                return;
            }
            dat.m.add_field( tripoint( x.get(), y.get(), dat.m.get_abs_sub().z ), chosen_id,
                             intensity, age );
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            ftype.check( oter_name, parameters );
        }
};
/**
 * Place an NPC.
 * "class": the npc class, see @ref map::place_npc
 */
class jmapgen_npc : public jmapgen_piece
{
    public:
        mapgen_value<string_id<npc_template>> npc_class;
        bool target;
        std::vector<trait_id> traits;
        jmapgen_npc( const JsonObject &jsi ) :
            npc_class( jsi.get_member( "class" ) )
            , target( jsi.get_bool( "target", false ) ) {
            if( jsi.has_string( "add_trait" ) ) {
                std::string new_trait = jsi.get_string( "add_trait" );
                traits.emplace_back();
                jsi.read( "add_trait", traits.back() );
            } else if( jsi.has_array( "add_trait" ) ) {
                jsi.read( "add_trait", traits );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            string_id<npc_template> chosen_id = npc_class.get( dat );
            if( chosen_id.is_null() ) {
                return;
            }
            character_id npc_id = dat.m.place_npc( point( x.get(), y.get() ), chosen_id );
            if( dat.mission() && target ) {
                dat.mission()->set_target_npc_id( npc_id );
            }
            npc *p = g->find_npc( npc_id );
            if( p != nullptr ) {
                for( const trait_id &new_trait : traits ) {
                    p->set_mutation( new_trait );
                }
            }
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            npc_class.check( oter_name, parameters );
        }
};
/**
* Place ownership area
*/
class jmapgen_faction : public jmapgen_piece
{
    public:
        mapgen_value<faction_id> id;
        jmapgen_faction( const JsonObject &jsi )
            : id( jsi.get_member( "id" ) ) {
        }
        mapgen_phase phase() const override {
            return mapgen_phase::faction_ownership;
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            faction_id chosen_id = id.get( dat );
            if( chosen_id.is_null() ) {
                return;
            }
            dat.m.apply_faction_ownership( point( x.val, y.val ), point( x.valmax, y.valmax ),
                                           chosen_id );
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            id.check( oter_name, parameters );
        }
};
/**
 * Place a sign with some text.
 * "signage": the text on the sign.
 */
class jmapgen_sign : public jmapgen_piece
{
    public:
        std::string signage;
        std::string snippet;
        jmapgen_sign( const JsonObject &jsi ) :
            signage( jsi.get_string( "signage", "" ) )
            , snippet( jsi.get_string( "snippet", "" ) ) {
            if( signage.empty() && snippet.empty() ) {
                jsi.throw_error( "jmapgen_sign: needs either signage or snippet" );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const point r( x.get(), y.get() );
            dat.m.furn_set( r, f_null );
            dat.m.furn_set( r, furn_str_id( "f_sign" ) );

            std::string signtext;

            if( !snippet.empty() ) {
                // select a snippet from the category
                signtext = SNIPPET.random_from_category( snippet ).value_or( translation() ).translated();
            } else if( !signage.empty() ) {
                signtext = signage;
            }
            if( !signtext.empty() ) {
                // replace tags
                signtext = _( signtext );

                std::string cityname = "illegible city name";
                tripoint abs_sub = dat.m.get_abs_sub();
                // TODO: fix point types
                const city *c = overmap_buffer.closest_city( tripoint_abs_sm( abs_sub ) ).city;
                if( c != nullptr ) {
                    cityname = c->name;
                }
                signtext = apply_all_tags( signtext, cityname );
            }
            dat.m.set_signage( tripoint( r, dat.m.get_abs_sub().z ), signtext );
        }
        std::string apply_all_tags( std::string signtext, const std::string &cityname ) const {
            replace_city_tag( signtext, cityname );
            replace_name_tags( signtext );
            return signtext;
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }
};
/**
 * Place graffiti with some text or a snippet.
 * "text": the text of the graffiti.
 * "snippet": snippet category to pull from for text instead.
 */
class jmapgen_graffiti : public jmapgen_piece
{
    public:
        std::string text;
        std::string snippet;
        jmapgen_graffiti( const JsonObject &jsi ) :
            text( jsi.get_string( "text", "" ) )
            , snippet( jsi.get_string( "snippet", "" ) ) {
            if( text.empty() && snippet.empty() ) {
                jsi.throw_error( "jmapgen_graffiti: needs either text or snippet" );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const point r( x.get(), y.get() );

            std::string graffiti;

            if( !snippet.empty() ) {
                // select a snippet from the category
                graffiti = SNIPPET.random_from_category( snippet ).value_or( translation() ).translated();
            } else if( !text.empty() ) {
                graffiti = text;
            }
            if( !graffiti.empty() ) {
                // replace tags
                graffiti = _( graffiti );

                std::string cityname = "illegible city name";
                tripoint abs_sub = dat.m.get_abs_sub();
                // TODO: fix point types
                const city *c = overmap_buffer.closest_city( tripoint_abs_sm( abs_sub ) ).city;
                if( c != nullptr ) {
                    cityname = c->name;
                }
                graffiti = apply_all_tags( graffiti, cityname );
            }
            dat.m.set_graffiti( tripoint( r, dat.m.get_abs_sub().z ), graffiti );
        }
        std::string apply_all_tags( std::string graffiti, const std::string &cityname ) const {
            replace_city_tag( graffiti, cityname );
            replace_name_tags( graffiti );
            return graffiti;
        }
};
/**
 * Place a vending machine with content.
 * "item_group": the item group that is used to generate the content of the vending machine.
 */
class jmapgen_vending_machine : public jmapgen_piece
{
    public:
        bool reinforced;
        mapgen_value<item_group_id> group_id;
        jmapgen_vending_machine( const JsonObject &jsi ) :
            reinforced( jsi.get_bool( "reinforced", false ) ) {
            if( jsi.has_member( "item_group" ) ) {
                group_id = mapgen_value<item_group_id>( jsi.get_member( "item_group" ) );
            } else {
                group_id = mapgen_value<item_group_id>( "default_vending_machine" );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const point r( x.get(), y.get() );
            dat.m.furn_set( r, f_null );
            item_group_id chosen_id = group_id.get( dat );
            if( chosen_id.is_null() ) {
                return;
            }
            dat.m.place_vending( r, chosen_id, reinforced );
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            group_id.check( oter_name, parameters );
        }
};
/**
 * Place a toilet with (dirty) water in it.
 * "amount": number of water charges to place.
 */
class jmapgen_toilet : public jmapgen_piece
{
    public:
        jmapgen_int amount;
        jmapgen_toilet( const JsonObject &jsi ) :
            amount( jsi, "amount", 0, 0 ) {
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const point r( x.get(), y.get() );
            const int charges = amount.get();
            dat.m.furn_set( r, f_null );
            if( charges == 0 ) {
                dat.m.place_toilet( r ); // Use the default charges supplied as default values
            } else {
                dat.m.place_toilet( r, charges );
            }
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }
};
/**
 * Place a gas pump with fuel in it.
 * "amount": number of fuel charges to place.
 */
class jmapgen_gaspump : public jmapgen_piece
{
    public:
        jmapgen_int amount;
        mapgen_value<itype_id> fuel;
        jmapgen_gaspump( const JsonObject &jsi ) :
            amount( jsi, "amount", 0, 0 ) {
            if( jsi.has_member( "fuel" ) ) {
                jsi.read( "fuel", fuel );
            }
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            fuel.check( oter_name, parameters );
            static const std::unordered_set<itype_id> valid_fuels = {
                itype_id::NULL_ID(), itype_gasoline, itype_diesel, itype_jp8, itype_avgas
            };
            for( const itype_id &possible_fuel : fuel.all_possible_results( parameters ) ) {
                // may want to not force this, if we want to support other fuels for some reason
                if( !valid_fuels.contains( possible_fuel ) ) {
                    debugmsg( "invalid fuel %s in %s", possible_fuel.str(), oter_name );
                }
            }
        }

        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const point r( x.get(), y.get() );
            int charges = amount.get();
            dat.m.furn_set( r, f_null );
            if( charges == 0 ) {
                charges = rng( 10000, 50000 );
            }
            itype_id chosen_fuel = fuel.get( dat );
            if( chosen_fuel.is_null() ) {
                dat.m.place_gas_pump( r, charges );
            } else {
                dat.m.place_gas_pump( r, charges, chosen_fuel );
            }
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }
};

/**
 * Place a specific liquid into the map.
 * "liquid": id of the liquid item (item should use charges)
 * "amount": quantity of liquid placed (a value of 0 uses the default amount)
 * "chance": chance of liquid being placed, see @ref map::place_items
 */
class jmapgen_liquid_item : public jmapgen_piece
{
    public:
        jmapgen_int amount;
        mapgen_value<itype_id> liquid;
        jmapgen_int chance;
        jmapgen_liquid_item( const JsonObject &jsi ) :
            amount( jsi, "amount", 0, 0 )
            , liquid( jsi.get_member( "liquid" ) )
            , chance( jsi, "chance", 1, 1 ) {
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            if( one_in( chance.get() ) ) {
                itype_id chosen_id = liquid.get( dat );
                if( chosen_id.is_null() ) {
                    return;
                }
                // Itemgroups apply migrations when being loaded, but we need to migrate
                // individual items here.
                itype_id migrated = item_controller->migrate_id( chosen_id );
                detached_ptr<item> newliquid = item::spawn( migrated, calendar::start_of_cataclysm );
                if( amount.valmax > 0 ) {
                    newliquid->charges = amount.get();
                }
                dat.m.add_item_or_charges( tripoint( x.get(), y.get(), dat.m.get_abs_sub().z ),
                                           std::move( newliquid ) );
            }
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            liquid.check( oter_name, parameters );
        }
};

/**
 * Place items from an item group.
 * "item": id of the item group.
 * "chance": chance of items being placed, see @ref map::place_items
 * "repeat": number of times to apply this piece
 */
class jmapgen_item_group : public jmapgen_piece
{
    public:
        item_group_id group_id;
        jmapgen_int chance;
        jmapgen_item_group( const JsonObject &jsi ) : chance( jsi, "chance", 1, 1 ) {
            JsonValue group = jsi.get_member( "item" );
            group_id = item_group::load_item_group( group, "collection" );
            repeat = jmapgen_int( jsi, "repeat", 1, 1 );
        }
        void check( const std::string &context, const mapgen_parameters & ) const override {
            if( !group_id.is_valid() ) {
                debugmsg( "Invalid item_group_id \"%s\" in %s", group_id.str(), context );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            dat.m.place_items( group_id, chance.get(), point( x.val, y.val ), point( x.valmax, y.valmax ), true,
                               calendar::start_of_cataclysm );
        }
};

/** Place items from an item group */
class jmapgen_loot : public jmapgen_piece
{
        friend jmapgen_objects;

    public:
        jmapgen_loot( const JsonObject &jsi ) :
            result_group( Item_group::Type::G_COLLECTION, 100, jsi.get_int( "ammo", 0 ),
                          jsi.get_int( "magazine", 0 ) )
            , chance( jsi.get_int( "chance", 100 ) ) {
            const item_group_id group = item_group_id( jsi.get_string( "group", std::string() ) );
            const itype_id ity = itype_id( jsi.get_string( "item", std::string() ) );

            if( group.is_empty() == ity.is_empty() ) {
                jsi.throw_error( "must provide either item or group" );
            }
            if( !group.is_empty() && !group.is_valid() ) {
                set_mapgen_defer( jsi, "group", "no such item group" );
            }
            if( !ity.is_empty() && !ity.is_valid() ) {
                set_mapgen_defer( jsi, "item", "no such item type '" + ity.str() + "'" );
            }

            // All the probabilities are 100 because we do the roll in @ref apply.
            if( !group ) {
                // Migrations are applied to item *groups* on load, but single item spawns must be
                // migrated individually
                result_group.add_item_entry( item_controller->migrate_id( ity ), 100 );
            } else {
                result_group.add_group_entry( group, 100 );
            }
        }

        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            if( rng( 0, 99 ) < chance ) {
                const Item_spawn_data *const isd = &result_group;
                std::vector<detached_ptr<item>> spawn = isd->create( calendar::start_of_cataclysm );
                dat.m.spawn_items( tripoint( rng( x.val, x.valmax ), rng( y.val, y.valmax ),
                                             dat.m.get_abs_sub().z ), std::move( spawn ) );
            }
        }

    private:
        Item_group result_group;
        int chance;
};

/**
 * Place spawn points for a monster group (actual monster spawning is done later).
 * "monster": id of the monster group.
 * "chance": see @ref map::place_spawns
 * "density": see @ref map::place_spawns
 */
class jmapgen_monster_group : public jmapgen_piece
{
    public:
        mapgen_value<mongroup_id> id;
        float density;
        jmapgen_int chance;
        jmapgen_monster_group( const JsonObject &jsi ) :
            id( jsi.get_member( "monster" ) )
            , density( jsi.get_float( "density", -1.0f ) )
            , chance( jsi, "chance", 1, 1 ) {
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            mongroup_id chosen_id = id.get( dat );
            if( chosen_id.is_null() ) {
                return;
            }
            dat.m.place_spawns( chosen_id, chance.get(), point( x.val, y.val ),
                                point( x.valmax, y.valmax ),
                                density == -1.0f ? dat.monster_density() : density );
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            id.check( oter_name, parameters );
        }
};
/**
 * Place spawn points for a specific monster.
 * "monster": id of the monster. or "group": id of the monster group.
 * "friendly": whether the new monster is friendly to the player character.
 * "name": the name of the monster (if it has one).
 * "chance": the percentage chance of a monster, affected by spawn density
 *     If high density means greater than one hundred percent, can place multiples.
 * "repeat": roll this many times for creatures, potentially spawning multiples.
 * "pack_size": place this many creatures each time a roll is successful.
 * "one_or_none": place max of 1 (or pack_size) monsters, even if spawn density > 1.
 *     Defaults to true if repeat and pack_size are unset, false if one is set.
 */
class jmapgen_monster : public jmapgen_piece
{
    public:
        weighted_int_list<mapgen_value<mtype_id>> ids;
        mapgen_value<mongroup_id> m_id;
        jmapgen_int chance;
        jmapgen_int pack_size;
        bool one_or_none;
        bool friendly;
        std::string name;
        bool target;
        jmapgen_monster( const JsonObject &jsi ) :
            chance( jsi, "chance", 100, 100 )
            , pack_size( jsi, "pack_size", 1, 1 )
            , one_or_none( jsi.get_bool( "one_or_none",
                                         !( jsi.has_member( "repeat" ) || jsi.has_member( "pack_size" ) ) ) )
            , friendly( jsi.get_bool( "friendly", false ) )
            , name( jsi.get_string( "name", "NONE" ) )
            , target( jsi.get_bool( "target", false ) ) {
            if( jsi.has_member( "group" ) ) {
                jsi.read( "group", m_id );
            } else if( jsi.has_array( "monster" ) ) {
                load_weighted_list( jsi.get_member( "monster" ), ids, 100 );
            } else {
                mapgen_value<mtype_id> id( jsi.get_member( "monster" ) );
                ids.add( id, 100 );
            }
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            for( const weighted_object<int, mapgen_value<mtype_id>> &id : ids ) {
                id.obj.check( oter_name, parameters );
            }
            m_id.check( oter_name, parameters );
        }

        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {

            int raw_odds = chance.get();

            // Handle spawn density: Increase odds, but don't let the odds of absence go below half the odds at density 1.
            // Instead, apply a multipler to the number of monsters for really high densities.
            // For example, a 50% chance at spawn density 4 becomes a 75% chance of ~2.7 monsters.
            int odds_after_density = raw_odds * get_option<float>( "SPAWN_DENSITY" );
            int max_odds = ( 100 + raw_odds ) / 2;
            float density_multiplier = 1;
            if( odds_after_density > max_odds ) {
                density_multiplier = 1.0f * odds_after_density / max_odds;
                odds_after_density = max_odds;
            }

            int mission_id = -1;
            if( dat.mission() && target ) {
                mission_id = dat.mission()->get_id();
            }

            int spawn_count = roll_remainder( density_multiplier );

            if( one_or_none ) { // don't let high spawn density alone cause more than 1 to spawn.
                spawn_count = std::min( spawn_count, 1 );
            }
            if( raw_odds == 100 ) { // don't spawn less than 1 if odds were 100%, even with low spawn density.
                spawn_count = std::max( spawn_count, 1 );
            } else {
                if( !x_in_y( odds_after_density, 100 ) ) {
                    return;
                }
            }

            mongroup_id chosen_group = m_id.get( dat );
            if( !chosen_group.is_null() ) {
                MonsterGroupResult spawn_details =
                    MonsterGroupManager::GetResultFromGroup( chosen_group );
                dat.m.add_spawn( spawn_details.name, spawn_count * pack_size.get(),
                { x.get(), y.get(), dat.m.get_abs_sub().z },
                friendly, -1, mission_id, name );
            } else {
                mtype_id chosen_type = ids.pick()->get( dat );
                if( !chosen_type.is_null() ) {
                    dat.m.add_spawn( chosen_type, spawn_count * pack_size.get(),
                    { x.get(), y.get(), dat.m.get_abs_sub().z },
                    friendly, -1, mission_id, name );
                }
            }
        }
};

/**
 * Place a vehicle.
 * "vehicle": id of the vehicle.
 * "chance": chance of spawning the vehicle: 0...100
 * "rotation": rotation of the vehicle, see @ref vehicle::vehicle
 * "fuel": fuel status of the vehicle, see @ref vehicle::vehicle
 * "status": overall (damage) status of the vehicle, see @ref vehicle::vehicle
 */
class jmapgen_vehicle : public jmapgen_piece
{
    public:
        mapgen_value<vgroup_id> type;
        jmapgen_int chance;
        std::vector<units::angle> rotation;
        int fuel;
        int status;
        jmapgen_vehicle( const JsonObject &jsi ) :
            type( jsi.get_member( "vehicle" ) )
            , chance( jsi, "chance", 1, 1 )
            //, rotation( jsi.get_int( "rotation", 0 ) ) // unless there is a way for the json parser to
            // return a single int as a list, we have to manually check this in the constructor below
            , fuel( jsi.get_int( "fuel", -1 ) )
            , status( jsi.get_int( "status", -1 ) ) {
            if( jsi.has_array( "rotation" ) ) {
                for( const JsonValue &elt : jsi.get_array( "rotation" ) ) {
                    rotation.push_back( units::from_degrees( elt.get_int() ) );
                }
            } else {
                rotation.push_back( units::from_degrees( jsi.get_int( "rotation", 0 ) ) );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            if( !x_in_y( chance.get(), 100 ) ) {
                return;
            }
            vgroup_id chosen_id = type.get( dat );
            if( chosen_id.is_null() ) {
                return;
            }
            dat.m.add_vehicle( chosen_id, point( x.get(), y.get() ), random_entry( rotation ),
                               fuel, status );
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            type.check( oter_name, parameters );
        }
};

/// Place a specific item.
class jmapgen_spawn_item : public jmapgen_piece
{
    public:
        mapgen_value<itype_id> type; //< id of item type to spawn.
        jmapgen_int amount;          //< amount of items to spawn.
        jmapgen_int chance;          //< chance of spawning it (1 = always, otherwise one_in(chance)).
        bool activate_on_spawn;      //< whether to activate the item on spawn.
        jmapgen_spawn_item( const JsonObject &jsi ) :
            type( jsi.get_member( "item" ) )
            , amount( jsi, "amount", 1, 1 )
            , chance( jsi, "chance", 100, 100 ) {
            repeat = jmapgen_int( jsi, "repeat", 1, 1 );
            activate_on_spawn = jsi.get_bool( "active", false );
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            itype_id chosen_id = type.get( dat );
            if( chosen_id.is_null() ) {
                return;
            }
            // Itemgroups apply migrations when being loaded, but we need to migrate
            // individual items here.
            chosen_id = item_controller->migrate_id( chosen_id );

            if( item_is_blacklisted( chosen_id ) ) {
                return;
            }

            const int c = chance.get();

            // 100% chance = exactly 1 item, otherwise scale by item spawn rate.
            const float spawn_rate = get_option<float>( "ITEM_SPAWNRATE" );
            const int spawn_count = ( c == 100 ) ? 1 : roll_remainder( c * spawn_rate / 100.0f );
            const int quantity = amount.get();

            const point p = { x.get(), y.get() };

            for( int i = 0; i < spawn_count; i++ ) {
                for( int j = 0; j < quantity; j++ ) {
                    detached_ptr<item> new_item = item::spawn( chosen_id, calendar::start_of_cataclysm );
                    if( activate_on_spawn ) {
                        new_item->activate();
                    }

                    dat.m.spawn_an_item( p, std::move( new_item ), 0, 0 );
                }
            }
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            type.check( oter_name, parameters );
        }
};
/**
 * Place a trap.
 * "trap": id of the trap.
 */
class jmapgen_trap : public jmapgen_piece
{
    public:
        mapgen_value<trap_id> id;
        jmapgen_trap( const JsonObject &jsi ) {
            init( jsi.get_member( "trap" ) );
        }

        explicit jmapgen_trap( const JsonValue &tid ) {
            if( tid.test_object() ) {
                JsonObject jo = tid.get_object();
                if( jo.has_member( "trap" ) ) {
                    init( jo.get_member( "trap" ) );
                    return;
                }
            }
            init( tid );
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            trap_id chosen_id = id.get( dat );
            if( chosen_id.id().is_null() ) {
                return;
            }
            const tripoint actual_loc = tripoint( x.get(), y.get(), dat.m.get_abs_sub().z );
            dat.m.trap_set( actual_loc, chosen_id );
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            id.check( oter_name, parameters );
        }
    private:
        void init( const JsonValue &jsi ) {
            id = mapgen_value<trap_id>( jsi );
        }
};
/**
 * Place a furniture.
 * "furn": id of the furniture.
 */
class jmapgen_furniture : public jmapgen_piece
{
    public:
        mapgen_value<furn_id> id;
        jmapgen_furniture( const JsonObject &jsi ) :
            jmapgen_furniture( jsi.get_member( "furn" ) ) {}
        explicit jmapgen_furniture( const JsonValue &fid ) : id( fid ) {}
        mapgen_phase phase() const override {
            return mapgen_phase::furniture;
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            furn_id chosen_id = id.get( dat );
            if( chosen_id.id().is_null() ) {
                return;
            }
            dat.m.furn_set( point( x.get(), y.get() ), chosen_id );
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            id.check( oter_name, parameters );
        }
};
/**
 * Place terrain.
 * "ter": id of the terrain.
 */
class jmapgen_terrain : public jmapgen_piece
{
    public:
        mapgen_value<ter_id> id;
        jmapgen_terrain( const JsonObject &jsi ) : jmapgen_terrain( jsi.get_member( "ter" ) ) {}
        explicit jmapgen_terrain( const JsonValue &tid ) : id( mapgen_value<ter_id>( tid ) ) {}

        bool is_nop() const override {
            return id.is_null();
        }
        mapgen_phase phase() const override {
            return mapgen_phase::terrain;
        }

        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            ter_id chosen_id = id.get( dat );
            if( chosen_id.id().is_null() ) {
                return;
            }
            dat.m.ter_set( point( x.get(), y.get() ), chosen_id );
            // Delete furniture if a wall was just placed over it. TODO: need to do anything for fluid, monsters?
            if( dat.m.has_flag_ter( "WALL", point( x.get(), y.get() ) ) ) {
                dat.m.furn_set( point( x.get(), y.get() ), f_null );
                // and items, unless the wall has PLACE_ITEM flag indicating it stores things.
                if( !dat.m.has_flag_ter( "PLACE_ITEM", point( x.get(), y.get() ) ) ) {
                    dat.m.i_clear( tripoint( x.get(), y.get(), dat.m.get_abs_sub().z ) );
                }
            }
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            id.check( oter_name, parameters );
        }
};
/**
 * Run a transformation.
 * "transform": id of the ter_furn_transform to run.
 */
class jmapgen_ter_furn_transform: public jmapgen_piece
{
    public:
        mapgen_value<ter_furn_transform_id> id;
        jmapgen_ter_furn_transform( const JsonObject &jsi ) :
            id( jsi.get_member( "transform" ) ) {}

        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            ter_furn_transform_id chosen_id = id.get( dat );
            if( chosen_id.is_null() ) {
                return;
            }
            chosen_id->transform( dat.m, tripoint( x.get(), y.get(), dat.m.get_abs_sub().z ) );
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            id.check( oter_name, parameters );
        }
};
/**
 * Calls @ref map::make_rubble to create rubble and destroy the existing terrain/furniture.
 * See map::make_rubble for explanation of the parameters.
 */
class jmapgen_make_rubble : public jmapgen_piece
{
    public:
        mapgen_value<furn_id> rubble_type = mapgen_value<furn_id>( f_rubble );
        mapgen_value<ter_id> floor_type = mapgen_value<ter_id>( t_dirt );
        bool overwrite = false;
        jmapgen_make_rubble( const JsonObject &jsi ) {
            if( jsi.has_member( "rubble_type" ) ) {
                rubble_type = mapgen_value<furn_id>( jsi.get_member( "rubble_type" ) );
            }
            if( jsi.has_member( "floor_type" ) ) {
                floor_type = mapgen_value<ter_id>( jsi.get_member( "floor_type" ) );
            }
            jsi.read( "overwrite", overwrite );
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            furn_id chosen_rubble_type = rubble_type.get( dat );
            ter_id chosen_floor_type = floor_type.get( dat );
            if( chosen_rubble_type.id().is_null() ) {
                return;
            }
            if( chosen_floor_type.id().is_null() ) {
                debugmsg( "null floor type when making rubble" );
                chosen_floor_type = t_dirt;
            }
            dat.m.make_rubble( tripoint( x.get(), y.get(), dat.m.get_abs_sub().z ),
                               chosen_rubble_type, chosen_floor_type, overwrite );
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            rubble_type.check( oter_name, parameters );
            floor_type.check( oter_name, parameters );
        }
};

/**
 * Place a computer (console) with given stats and effects.
 * @param options Array of @ref computer_option
 * @param failures Array of failure effects (see @ref computer_failure)
 */
class jmapgen_computer : public jmapgen_piece
{
    public:
        translation name;
        translation access_denied;
        int security;
        std::vector<computer_option> options;
        std::vector<computer_failure> failures;
        bool target;
        jmapgen_computer( const JsonObject &jsi ) {
            jsi.read( "name", name );
            jsi.read( "access_denied", access_denied );
            security = jsi.get_int( "security", 0 );
            target = jsi.get_bool( "target", false );
            if( jsi.has_array( "options" ) ) {
                for( JsonObject jo : jsi.get_array( "options" ) ) {
                    options.emplace_back( computer_option::from_json( jo ) );
                }
            }
            if( jsi.has_array( "failures" ) ) {
                for( JsonObject jo : jsi.get_array( "failures" ) ) {
                    failures.emplace_back( computer_failure::from_json( jo ) );
                }
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const point r{ x.get(), y.get() };
            dat.m.ter_set( r, t_console );
            dat.m.furn_set( r, f_null );
            computer *cpu = dat.m.add_computer( tripoint( r, dat.m.get_abs_sub().z ), name.translated(),
                                                security );
            for( const auto &opt : options ) {
                cpu->add_option( opt );
            }
            for( const auto &opt : failures ) {
                cpu->add_failure( opt );
            }
            if( target && dat.mission() ) {
                cpu->set_mission( dat.mission()->get_id() );
            }

            // The default access denied message is defined in computer's constructor
            if( !access_denied.empty() ) {
                cpu->set_access_denied_msg( access_denied.translated() );
            }
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }
};

/**
 * Place an item in furniture (expected to be used with NOITEM SEALED furniture like plants).
 * "item": item to spawn (object with usual parameters).
 * "items": item group to spawn (object with usual parameters).
 * "furniture": furniture to create around it.
 */
class jmapgen_sealed_item : public jmapgen_piece
{
    public:
        mapgen_value<furn_id> furniture;
        jmapgen_int chance;
        std::optional<jmapgen_spawn_item> item_spawner;
        std::optional<jmapgen_item_group> item_group_spawner;
        jmapgen_sealed_item( const JsonObject &jsi )
            : furniture( jsi.get_member( "furniture" ) )
            , chance( jsi, "chance", 100, 100 ) {
            if( jsi.has_object( "item" ) ) {
                JsonObject item_obj = jsi.get_object( "item" );
                item_spawner = jmapgen_spawn_item( item_obj );
            }
            if( jsi.has_object( "items" ) ) {
                JsonObject items_obj = jsi.get_object( "items" );
                item_group_spawner = jmapgen_item_group( items_obj );
            }
        }

        void check( const std::string &oter_name, const mapgen_parameters &params ) const override {
            std::string short_summary =
                string_format( "sealed_item special in json mapgen for %s", oter_name );
            if( !item_spawner && !item_group_spawner ) {
                debugmsg( "%s specifies neither an item nor an item group.  "
                          "It should specify at least one.",
                          short_summary );
                return;
            }

            for( const furn_str_id &f : furniture.all_possible_results( params ) ) {
                std::string summary =
                    string_format( "%s using furniture %s", short_summary, f.str() );

                if( !f.is_valid() ) {
                    debugmsg( "%s which is not valid furniture", summary );
                    return;
                }

                const furn_t &furn = *f;

                if( furn.has_flag( "PLANT" ) ) {
                    // plant furniture requires exactly one seed item within it
                    if( item_spawner && item_group_spawner ) {
                        debugmsg( "%s (with flag PLANT) specifies both an item and an item group.  "
                                  "It should specify exactly one.",
                                  summary );
                        return;
                    }

                    if( item_spawner ) {
                        item_spawner->check( oter_name, params );
                        int count = item_spawner->amount.get();
                        if( count != 1 ) {
                            debugmsg( "%s (with flag PLANT) spawns %d items; it should spawn "
                                      "exactly one.", summary, count );
                            return;
                        }
                        int item_chance = item_spawner->chance.get();
                        if( item_chance != 100 ) {
                            debugmsg( "%s (with flag PLANT) spawns an item with probability %d%%; "
                                      "it should always spawn.  You can move the \"chance\" up to "
                                      "the sealed_item instead of the \"item\" within.",
                                      summary, item_chance );
                            return;
                        }
                        for( const itype_id &t :
                             item_spawner->type.all_possible_results( params ) ) {
                            if( !t->seed ) {
                                debugmsg( "%s (with flag PLANT) spawns item type %s which is not a "
                                          "seed.", summary, t.str() );
                                return;
                            }
                        }
                    }

                    if( item_group_spawner ) {
                        item_group_spawner->check( oter_name, params );
                        int ig_chance = item_group_spawner->chance.get();
                        if( ig_chance != 100 ) {
                            debugmsg( "%s (with flag PLANT) spawns item group %s with chance %d.  "
                                      "It should have chance 100.  You can move the \"chance\" up "
                                      "to the sealed_item instead of the \"items\" within.",
                                      summary, item_group_spawner->group_id.str(), ig_chance );
                            return;
                        }
                        item_group_id group_id = item_group_spawner->group_id;
                        for( const itype *type :
                             item_group::every_possible_item_from( group_id ) ) {
                            if( !type->seed ) {
                                debugmsg( "%s (with flag PLANT) spawns item group %s which can "
                                          "spawn item %s which is not a seed.",
                                          summary, group_id.str(), type->get_id().str() );
                                return;
                            }
                        }

                    }
                }
            }
        }

        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const int c = chance.get();

            // 100% chance = always generate, otherwise scale by item spawn rate.
            // (except is capped at 1)
            const float spawn_rate = get_option<float>( "ITEM_SPAWNRATE" );
            if( c != 100 && !x_in_y( c * spawn_rate / 100.0f, 1 ) ) {
                return;
            }

            dat.m.furn_set( point( x.get(), y.get() ), f_null );
            if( item_spawner ) {
                item_spawner->apply( dat, x, y );
            }
            if( item_group_spawner ) {
                item_group_spawner->apply( dat, x, y );
            }
            furn_id chosen_furn = furniture.get( dat );
            dat.m.furn_set( point( x.get(), y.get() ), chosen_furn );
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            return dat.m.veh_at( tripoint( p, dat.zlevel() ) ).has_value();
        }
};
/**
 * Translate terrain from one ter_id to another.
 * "from": id of the starting terrain.
 * "to": id of the ending terrain
 * not useful for normal mapgen, very useful for mapgen_update
 */
class jmapgen_translate : public jmapgen_piece
{
    public:
        mapgen_value<ter_id> from;
        mapgen_value<ter_id> to;
        jmapgen_translate( const JsonObject &jsi )
            : from( jsi.get_member( "from" ) )
            , to( jsi.get_member( "to" ) ) {
        }
        mapgen_phase phase() const override {
            return mapgen_phase::transform;
        }
        void apply( const mapgendata &dat, const jmapgen_int &/*x*/,
                    const jmapgen_int &/*y*/ ) const override {
            ter_id chosen_from = from.get( dat );
            ter_id chosen_to = to.get( dat );
            dat.m.translate( chosen_from, chosen_to );
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            from.check( oter_name, parameters );
            to.check( oter_name, parameters );
        }
};
/**
 * Place a zone
 */
class jmapgen_zone : public jmapgen_piece
{
    public:
        mapgen_value<zone_type_id> zone_type;
        mapgen_value<faction_id> faction;
        std::string name;
        jmapgen_zone( const JsonObject &jsi )
            : zone_type( jsi.get_member( "type" ) )
            , faction( jsi.get_member( "faction" ) ) {
            if( jsi.has_string( "name" ) ) {
                name = jsi.get_string( "name" );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            zone_type_id chosen_zone_type = zone_type.get( dat );
            faction_id chosen_faction = faction.get( dat );
            zone_manager &mgr = zone_manager::get_manager();
            const tripoint start = dat.m.getabs( tripoint( x.val, y.val, 0 ) );
            const tripoint end = dat.m.getabs( tripoint( x.valmax, y.valmax, 0 ) );
            mgr.add( name, chosen_zone_type, chosen_faction, false, true, start, end );
        }

        void check( const std::string &oter_name, const mapgen_parameters &parameters
                  ) const override {
            zone_type.check( oter_name, parameters );
            faction.check( oter_name, parameters );
        }
};

/**
 * Calls another mapgen call inside the current one.
 * Note: can't use regular overmap ids.
 * @param entries list of pairs [nested mapgen id, weight].
 */
class jmapgen_nested : public jmapgen_piece
{
    private:
        class neighbor_oter_check
        {
            private:
                std::unordered_map<direction, cata::flat_set<oter_type_str_id>> neighbors;
            public:
                explicit neighbor_oter_check( const JsonObject &jsi ) {
                    for( direction dir : all_enum_values<direction>() ) {
                        cata::flat_set<oter_type_str_id> dir_neighbours =
                            jsi.get_tags<oter_type_str_id, cata::flat_set<oter_type_str_id>>(
                                io::enum_to_string( dir ) );
                        if( !dir_neighbours.empty() ) {
                            neighbors[dir] = std::move( dir_neighbours );
                        }
                    }
                }

                void check( const std::string &oter_name ) const {
                    for( const auto &p : neighbors ) {
                        for( const oter_type_str_id &id : p.second ) {
                            if( !id.is_valid() ) {
                                debugmsg( "Invalid oter_type_str_id '%s' in %s", id.str(), oter_name );
                            }
                        }
                    }
                }

                bool test( const mapgendata &dat ) const {
                    for( const std::pair<const direction, cata::flat_set<oter_type_str_id>> &p :
                         neighbors ) {
                        const direction dir = p.first;
                        const cata::flat_set<oter_type_str_id> &allowed_neighbors = p.second;

                        assert( !allowed_neighbors.empty() );

                        bool this_direction_matches = false;
                        for( const oter_type_str_id &allowed_neighbor : allowed_neighbors ) {
                            this_direction_matches |=
                                is_ot_match( allowed_neighbor.str(), dat.neighbor_at( dir ).id(),
                                             ot_match_type::contains );
                        }
                        if( !this_direction_matches ) {
                            return false;
                        }
                    }
                    return true;
                }
        };

        class neighbor_join_check
        {
            private:
                std::unordered_map<cube_direction, cata::flat_set<std::string>> neighbors;
            public:
                explicit neighbor_join_check( const JsonObject &jsi ) {
                    for( cube_direction dir : all_enum_values<cube_direction>() ) {
                        cata::flat_set<std::string> dir_neighbours =
                            jsi.get_tags<std::string, cata::flat_set<std::string>>(
                                io::enum_to_string( dir ) );
                        if( !dir_neighbours.empty() ) {
                            neighbors[dir] = std::move( dir_neighbours );
                        }
                    }
                }

                void check( const std::string & ) const {
                    // TODO: check join ids are valid
                }

                bool test( const mapgendata &dat ) const {
                    for( const std::pair<const cube_direction, cata::flat_set<std::string>> &p :
                         neighbors ) {
                        const cube_direction dir = p.first;
                        const cata::flat_set<std::string> &allowed_joins = p.second;

                        assert( !allowed_joins.empty() );

                        bool this_direction_matches = false;
                        for( const std::string &allowed_join : allowed_joins ) {
                            this_direction_matches |= dat.has_join( dir, allowed_join );
                        }
                        if( !this_direction_matches ) {
                            return false;
                        }
                    }
                    return true;
                }
        };

        class neighbor_connection_check
        {
            private:
                std::unordered_map<om_direction::type, std::set<overmap_connection_id>> neighbors;
            public:
                neighbor_connection_check( const JsonObject &jsi ) {
                    for( om_direction::type dir : om_direction::all ) {
                        std::set<overmap_connection_id> dir_connections = jsi.get_tags<overmap_connection_id>
                                ( io::enum_to_string( dir ) );
                        if( !dir_connections.empty() ) {
                            neighbors[dir] = std::move( dir_connections );
                        }
                    }
                }

                void check( const std::string &oter_name ) const {
                    for( const auto &p : neighbors ) {
                        for( const overmap_connection_id &id : p.second ) {
                            if( !id.is_valid() ) {
                                debugmsg( "Invalid overmap_connection_id '%s' in %s", id.str(), oter_name );
                            }
                        }
                    }
                }

                bool test( const mapgendata &dat ) const {
                    for( const auto &p : neighbors ) {
                        const om_direction::type dir = p.first;
                        const std::set<overmap_connection_id> &allowed_connections = p.second;

                        bool this_direction_matches = false;
                        for( const overmap_connection_id &connection : allowed_connections ) {
                            const oter_id neighbor = dat.neighbor_at( dir );
                            this_direction_matches |= connection->has( neighbor ) &&
                                                      neighbor->has_connection( om_direction::opposite( dir ) );
                        }
                        if( !this_direction_matches ) {
                            return false;
                        }
                    }
                    return true;
                }
        };

    public:
        weighted_int_list<std::string> entries;
        weighted_int_list<std::string> else_entries;
        neighbor_oter_check neighbor_oters;
        neighbor_join_check neighbor_joins;
        neighbor_connection_check neighbor_connections;
        jmapgen_nested( const JsonObject &jsi )
            : neighbor_oters( jsi.get_object( "neighbors" ) )
            , neighbor_joins( jsi.get_object( "joins" ) )
            , neighbor_connections( jsi.get_object( "connections" ) ) {
            if( jsi.has_member( "chunks" ) ) {
                load_weighted_list( jsi.get_member( "chunks" ), entries, 100 );
            }
            if( jsi.has_member( "else_chunks" ) ) {
                load_weighted_list( jsi.get_member( "else_chunks" ), else_entries, 100 );
            }
        }

        const weighted_int_list<std::string> &get_entries( const mapgendata &dat ) const {
            if( neighbor_oters.test( dat ) && neighbor_joins.test( dat ) && neighbor_connections.test( dat ) ) {
                return entries;
            } else {
                return else_entries;
            }
        }
        mapgen_phase phase() const override {
            return mapgen_phase::nested_mapgen;
        }
        void merge_parameters_into( mapgen_parameters &params,
                                    const std::string &outer_context ) const override {
            auto merge_from = [&]( const std::string & name ) {
                if( name == "null" ) {
                    return;
                }
                const auto iter = nested_mapgen.find( name );
                if( iter == nested_mapgen.end() ) {
                    debugmsg( "Unknown nested mapgen function id %s", name );
                    return;
                }
                using Obj = weighted_object<int, std::shared_ptr<mapgen_function_json_nested>>;
                for( const Obj &nested : iter->second ) {
                    nested.obj->merge_non_nest_parameters_into( params, outer_context );
                }
            };

            for( const weighted_object<int, std::string> &name : entries ) {
                merge_from( name.obj );
            }

            for( const weighted_object<int, std::string> &name : else_entries ) {
                merge_from( name.obj );
            }
        }
        void apply( const mapgendata &dat, const jmapgen_int &x, const jmapgen_int &y
                  ) const override {
            const std::string *res = get_entries( dat ).pick();
            if( res == nullptr || res->empty() || *res == "null" ) {
                // This will be common when neighbors.test(...) is false, since else_entires is often empty.
                return;
            }

            const auto iter = nested_mapgen.find( *res );
            if( iter == nested_mapgen.end() ) {
                debugmsg( "Unknown nested mapgen function id %s", res->c_str() );
                return;
            }

            // A second roll? Let's allow it for now
            const auto &ptr = iter->second.pick();
            if( ptr == nullptr ) {
                return;
            }

            ( *ptr )->nest( dat, point( x.get(), y.get() ) );
        }

        void check( const std::string &oter_name, const mapgen_parameters & ) const override {
            neighbor_oters.check( oter_name );
            neighbor_joins.check( oter_name );
            neighbor_connections.check( oter_name );
        }
        bool has_vehicle_collision( const mapgendata &dat, const point &p ) const override {
            const weighted_int_list<std::string> &selected_entries = get_entries( dat );

            if( selected_entries.empty() ) {
                return false;
            }

            for( auto &entry : selected_entries ) {
                if( entry.obj == "null" ) {
                    continue;
                }
                const auto iter = nested_mapgen.find( entry.obj );
                if( iter == nested_mapgen.end() ) {
                    return false;
                }
                for( const auto &nest : iter->second ) {
                    if( nest.obj->has_vehicle_collision( dat, p ) ) {
                        return true;
                    }
                }
            }

            return false;
        }
};

jmapgen_objects::jmapgen_objects( point offset, point mapsize, point tot_size )
    : m_offset( offset )
    , mapgensize( mapsize )
    , total_size( tot_size )
{}

bool jmapgen_objects::check_bounds( const jmapgen_place &place, const JsonObject &jso )
{
    return common_check_bounds( place.x, place.y, mapgensize, jso );
}

void jmapgen_objects::add( const jmapgen_place &place,
                           const shared_ptr_fast<const jmapgen_piece> &piece )
{
    objects.emplace_back( place, piece );
}

template<typename PieceType>
void jmapgen_objects::load_objects( JsonArray parray )
{
    for( JsonObject jsi : parray ) {
        jmapgen_place where( jsi );
        where.offset( m_offset );

        if( check_bounds( where, jsi ) ) {
            add( where, make_shared_fast<PieceType>( jsi ) );
        } else {
            jsi.allow_omitted_members();
        }
    }
}

template<>
void jmapgen_objects::load_objects<jmapgen_loot>( JsonArray parray )
{
    for( JsonObject jsi : parray ) {
        jmapgen_place where( jsi );
        where.offset( m_offset );

        if( !check_bounds( where, jsi ) ) {
            jsi.allow_omitted_members();
            continue;
        }

        auto loot = make_shared_fast<jmapgen_loot>( jsi );
        auto rate = get_option<float>( "ITEM_SPAWNRATE" );

        if( where.repeat.valmax != 1 ) {
            // if loot can repeat scale according to rate
            where.repeat.val = std::max( static_cast<int>( where.repeat.val * rate ), 1 );
            where.repeat.valmax = std::max( static_cast<int>( where.repeat.valmax * rate ), 1 );

        } else if( loot->chance != 100 ) {
            // otherwise except where chance is 100% scale probability
            loot->chance = std::max( std::min( static_cast<int>( loot->chance * rate ), 100 ), 1 );
        }

        add( where, loot );
    }
}

template<typename PieceType>
void jmapgen_objects::load_objects( const JsonObject &jsi, const std::string &member_name )
{
    if( !jsi.has_member( member_name ) ) {
        return;
    }
    load_objects<PieceType>( jsi.get_array( member_name ) );
}

template<typename PieceType>
void load_place_mapings( const JsonObject &jobj, mapgen_palette::placing_map::mapped_type &vect )
{
    vect.push_back( make_shared_fast<PieceType>( jobj ) );
}

/*
This is the default load function for mapgen pieces that only support loading from a json object,
not from a simple string.
Most non-trivial mapgen pieces (like item spawn which contains at least the item group and chance)
are like this. Other pieces (trap, furniture ...) can be loaded from a single string and have
an overload below.
The mapgen piece is loaded from the member of the json object named key.
*/
template<typename PieceType>
void load_place_mapings( const JsonValue &value, mapgen_palette::placing_map::mapped_type &vect )
{
    if( value.test_object() ) {
        load_place_mapings<PieceType>( value.get_object(), vect );
    } else {
        for( JsonObject jo : value.get_array() ) {
            load_place_mapings<PieceType>( jo, vect );
            jo.allow_omitted_members();
        }
    }
}

/*
This function allows loading the mapgen pieces from a single string, *or* a json object.
*/
template<typename PieceType>
void load_place_mapings_string( const JsonValue &value,
                                mapgen_palette::placing_map::mapped_type &vect )
{
    if( value.test_string() || value.test_object() ) {
        try {
            vect.push_back( make_shared_fast<PieceType>( value ) );
        } catch( const std::runtime_error &err ) {
            // Using the json object here adds nice formatting and context information
            value.throw_error( err.what() );
        }
    } else {
        for( const JsonValue entry : value.get_array() ) {
            if( entry.test_string() ) {
                try {
                    vect.push_back( make_shared_fast<PieceType>( entry ) );
                } catch( const std::runtime_error &err ) {
                    // Using the json object here adds nice formatting and context information
                    entry.throw_error( err.what() );
                }
            } else {
                load_place_mapings<PieceType>( entry.get_object(), vect );
            }
        }
    }
}
/*
This function is like load_place_mapings_string, except if the input is an array it will create an
instance of jmapgen_alternatively which will chose the mapgen piece to apply to the map randomly.
Use this with terrain or traps or other things that can not be applied twice to the same place.
*/
template<typename PieceType>
void load_place_mapings_alternatively( const JsonValue &value,
                                       mapgen_palette::placing_map::mapped_type &vect )
{
    if( !value.test_array() ) {
        load_place_mapings_string<PieceType>( value, vect );
    } else {
        auto alter = make_shared_fast< jmapgen_alternatively<PieceType> >();
        for( const JsonValue entry : value.get_array() ) {
            if( entry.test_string() ) {
                try {
                    alter->alternatives.emplace_back( entry );
                } catch( const std::runtime_error &err ) {
                    // Using the json object here adds nice formatting and context information
                    entry.throw_error( err.what() );
                }
            } else if( entry.test_object() ) {
                JsonObject jsi = entry.get_object();
                alter->alternatives.emplace_back( jsi );
            } else if( entry.test_array() ) {
                // If this is an array, it means it is an entry followed by a desired total count of instances.
                JsonArray piece_and_count_jarr = entry.get_array();
                if( piece_and_count_jarr.size() != 2 ) {
                    piece_and_count_jarr.throw_error( "Array must have exactly two entries: the object, then the count." );
                }

                // Test if this is a string or object, and then just emplace it.
                if( piece_and_count_jarr.test_string() || piece_and_count_jarr.test_object() ) {
                    try {
                        alter->alternatives.emplace_back( piece_and_count_jarr.next() );
                    } catch( const std::runtime_error &err ) {
                        piece_and_count_jarr.throw_error( err.what() );
                    }
                } else {
                    piece_and_count_jarr.throw_error( "First entry must be a string or object." );
                }

                if( piece_and_count_jarr.test_int() ) {
                    // We already emplaced the first instance, so do one less.
                    int repeat = std::max( 0, piece_and_count_jarr.next_int() - 1 );
                    PieceType piece_to_repeat = alter->alternatives.back();
                    for( int i = 0; i < repeat; i++ ) {
                        alter->alternatives.emplace_back( piece_to_repeat );
                    }
                } else {
                    piece_and_count_jarr.throw_error( "Second entry must be an integer." );
                }
            }
        }
        vect.push_back( alter );
    }
}

template<>
void load_place_mapings<jmapgen_trap>( const JsonValue &value,
                                       mapgen_palette::placing_map::mapped_type &vect )
{
    load_place_mapings_alternatively<jmapgen_trap>( value, vect );
}

template<>
void load_place_mapings<jmapgen_furniture>( const JsonValue &value,
        mapgen_palette::placing_map::mapped_type &vect )
{
    load_place_mapings_alternatively<jmapgen_furniture>( value, vect );
}

template<>
void load_place_mapings<jmapgen_terrain>( const JsonValue &value,
        mapgen_palette::placing_map::mapped_type &vect )
{
    load_place_mapings_alternatively<jmapgen_terrain>( value, vect );
}

template<typename PieceType>
void mapgen_palette::load_place_mapings( const JsonObject &jo, const std::string &member_name,
        placing_map &format_placings )
{
    if( jo.has_object( "mapping" ) ) {
        for( const JsonMember member : jo.get_object( "mapping" ) ) {
            const map_key key( member );
            JsonObject sub = member.get_object();
            sub.allow_omitted_members();
            if( !sub.has_member( member_name ) ) {
                continue;
            }
            auto &vect = format_placings[ key ];
            ::load_place_mapings<PieceType>( sub.get_member( member_name ), vect );
        }
    }
    if( !jo.has_object( member_name ) ) {
        return;
    }
    for( const JsonMember member : jo.get_object( member_name ) ) {
        const map_key key( member );
        auto &vect = format_placings[ key ];
        ::load_place_mapings<PieceType>( member, vect );
    }
}

static std::map<palette_id, mapgen_palette> palettes;

template<>
const mapgen_palette &string_id<mapgen_palette>::obj() const
{
    auto it = palettes.find( *this );
    if( it == palettes.end() ) {
        static const mapgen_palette null_palette;
        return null_palette;
    }
    return it->second;
}

template<>
bool string_id<mapgen_palette>::is_valid() const
{
    return palettes.find( *this ) != palettes.end();
}

void mapgen_palette::check()
{
    std::string context = "palette " + id.str();
    mapgen_parameters no_parameters;
    for( const std::pair<const std::string, mapgen_parameter> &param : parameters.map ) {
        std::string this_context = string_format( "parameter %s in %s", param.first, context );
        param.second.check( no_parameters, this_context );
    }
    for( const std::pair<const map_key, std::vector<shared_ptr_fast<const jmapgen_piece>>> &p :
         format_placings ) {
        for( const shared_ptr_fast<const jmapgen_piece> &j : p.second ) {
            j->check( context, parameters );
        }
    }
}

mapgen_palette mapgen_palette::load_temp( const JsonObject &jo, const std::string &src,
        const std::string &context )
{
    return load_internal( jo, src, context, false, true );
}

void mapgen_palette::load( const JsonObject &jo, const std::string &src )
{
    mapgen_palette ret = load_internal( jo, src, "", true, false );
    if( ret.id.is_empty() ) {
        jo.throw_error( "Named palette needs an id" );
    }

    palettes[ ret.id ] = ret;
}

const mapgen_palette &mapgen_palette::get( const palette_id &id )
{
    const auto iter = palettes.find( id );
    if( iter != palettes.end() ) {
        return iter->second;
    }

    debugmsg( "Requested palette with unknown id %s", id.c_str() );
    static mapgen_palette dummy;
    return dummy;
}

void mapgen_palette::check_definitions()
{
    for( auto &p : palettes ) {
        p.second.check();
    }
}

void mapgen_palette::reset()
{
    palettes.clear();
}

void mapgen_palette::add( const mapgen_value<std::string> &rh, const add_palette_context &context )
{
    std::vector<std::string> possible_values = rh.all_possible_results( *context.parameters );
    assert( !possible_values.empty() );
    if( possible_values.size() == 1 ) {
        add( palette_id( possible_values.front() ), context );
    } else {
        const auto param_it =
            context.parameters->add_unique_parameter(
                "palette_choice_", rh, cata_variant_type::palette_id,
                mapgen_parameter_scope::overmap_special );
        const std::string &param_name = param_it->first;
        add_palette_context context_with_extra_constraint( context );
        for( const std::string &value : possible_values ) {
            palette_id val_id( value );
            context_with_extra_constraint.constraints.emplace_back( param_name, val_id );
            add( val_id, context_with_extra_constraint );
            context_with_extra_constraint.constraints.pop_back();
        }
    }
}

void mapgen_palette::add( const palette_id &rh, const add_palette_context &context )
{
    add( get( rh ), context );
}

void mapgen_palette::add( const mapgen_palette &rh, const add_palette_context &context )
{
    std::string actual_context = id.is_empty() ? context.context : "palette " + id.str();

    if( !rh.id.is_empty() ) {
        const std::vector<palette_id> &ancestors = context.ancestors;
        auto loop_start = std::find( ancestors.begin(), ancestors.end(), rh.id );
        if( loop_start != ancestors.end() ) {
            std::string loop_ids = enumerate_as_string( loop_start, ancestors.end(),
            []( const palette_id & i ) {
                return i.str();
            }, enumeration_conjunction::arrow );
            debugmsg( "loop in palette references: %s", loop_ids );
            return;
        }
    }
    add_palette_context new_context = context;
    new_context.ancestors.push_back( rh.id );

    for( const mapgen_value<std::string> &recursive_palette : rh.palettes_used ) {
        add( recursive_palette, new_context );
    }
    for( const auto &placing : rh.format_placings ) {
        const std::vector<mapgen_constraint<palette_id>> &constraints = context.constraints;
        std::vector<shared_ptr_fast<const jmapgen_piece>> constrained_placings = placing.second;
        if( !constraints.empty() ) {
            for( shared_ptr_fast<const jmapgen_piece> &piece : constrained_placings ) {
                piece = make_shared_fast<jmapgen_constrained<palette_id>>(
                            std::move( piece ), constraints );
            }
        }
        std::vector<shared_ptr_fast<const jmapgen_piece>> &these_placings =
                    format_placings[placing.first];
        these_placings.insert( these_placings.end(),
                               constrained_placings.begin(), constrained_placings.end() );
    }
    for( const auto &placing : rh.keys_with_terrain ) {
        keys_with_terrain.insert( placing );
    }
    parameters.check_and_merge( rh.parameters, actual_context );
}

mapgen_palette mapgen_palette::load_internal( const JsonObject &jo, const std::string &,
        const std::string &context, bool require_id, bool allow_recur )
{
    mapgen_palette new_pal;
    auto &format_placings = new_pal.format_placings;
    auto &keys_with_terrain = new_pal.keys_with_terrain;
    if( require_id ) {
        new_pal.id = palette_id( jo.get_string( "id" ) );
    }

    jo.read( "parameters", new_pal.parameters.map );

    if( jo.has_array( "palettes" ) ) {
        jo.read( "palettes", new_pal.palettes_used );
        if( allow_recur ) {
            // allow_recur means that it's safe to assume all the palettes have
            // been defined and we can inline now.  Otherwise we just leave the
            // list in our palettes_used array and it will be consumed
            // recursively by calls to add which add this palette.
            add_palette_context add_context{ context, &new_pal.parameters };
            for( auto &p : new_pal.palettes_used ) {
                new_pal.add( p, add_context );
            }
            new_pal.palettes_used.clear();
        }
    }

    // mandatory: every character in rows must have matching entry, unless fill_ter is set
    // "terrain": { "a": "t_grass", "b": "t_lava" }.  To help enforce this we
    // keep track of everything in the "terrain" object
    if( jo.has_member( "terrain" ) ) {
        for( const JsonMember member : jo.get_object( "terrain" ) ) {
            keys_with_terrain.insert( map_key( member ) );
        }
    }

    std::string c = "palette " + new_pal.id.str();
    new_pal.load_place_mapings<jmapgen_terrain>( jo, "terrain", format_placings );
    new_pal.load_place_mapings<jmapgen_furniture>( jo, "furniture", format_placings );
    new_pal.load_place_mapings<jmapgen_field>( jo, "fields", format_placings );
    new_pal.load_place_mapings<jmapgen_npc>( jo, "npcs", format_placings );
    new_pal.load_place_mapings<jmapgen_sign>( jo, "signs", format_placings );
    new_pal.load_place_mapings<jmapgen_vending_machine>( jo, "vendingmachines", format_placings );
    new_pal.load_place_mapings<jmapgen_toilet>( jo, "toilets", format_placings );
    new_pal.load_place_mapings<jmapgen_gaspump>( jo, "gaspumps", format_placings );
    new_pal.load_place_mapings<jmapgen_item_group>( jo, "items", format_placings );
    new_pal.load_place_mapings<jmapgen_monster_group>( jo, "monsters", format_placings );
    new_pal.load_place_mapings<jmapgen_vehicle>( jo, "vehicles", format_placings );
    // json member name is not optimal, it should be plural like all the others above, but that conflicts
    // with the items entry with refers to item groups.
    new_pal.load_place_mapings<jmapgen_spawn_item>( jo, "item", format_placings );
    new_pal.load_place_mapings<jmapgen_trap>( jo, "traps", format_placings );
    new_pal.load_place_mapings<jmapgen_monster>( jo, "monster", format_placings );
    new_pal.load_place_mapings<jmapgen_make_rubble>( jo, "rubble", format_placings );
    new_pal.load_place_mapings<jmapgen_computer>( jo, "computers", format_placings );
    new_pal.load_place_mapings<jmapgen_sealed_item>( jo, "sealed_item", format_placings );
    new_pal.load_place_mapings<jmapgen_nested>( jo, "nested", format_placings );
    new_pal.load_place_mapings<jmapgen_liquid_item>( jo, "liquids", format_placings );
    new_pal.load_place_mapings<jmapgen_graffiti>( jo, "graffiti", format_placings );
    new_pal.load_place_mapings<jmapgen_translate>( jo, "translate", format_placings );
    new_pal.load_place_mapings<jmapgen_zone>( jo, "zones", format_placings );
    new_pal.load_place_mapings<jmapgen_ter_furn_transform>( jo, "ter_furn_transforms",
            format_placings );
    new_pal.load_place_mapings<jmapgen_faction>( jo, "faction_owner_character", format_placings );

    for( mapgen_palette::placing_map::value_type &p : format_placings ) {
        p.second.erase(
            std::remove_if(
                p.second.begin(), p.second.end(),
        []( const shared_ptr_fast<const jmapgen_piece> &placing ) {
            return placing->is_nop();
        } ), p.second.end() );
    }
    return new_pal;
}

mapgen_palette::add_palette_context::add_palette_context(
    const std::string &ctx, mapgen_parameters *params )
    : context( ctx )
    , parameters( params )
{}

bool mapgen_function_json::setup_internal( const JsonObject &jo )
{
    // Just to make sure no one does anything stupid
    if( jo.has_member( "mapgensize" ) ) {
        jo.throw_error( "\"mapgensize\" only allowed for nested mapgen" );
    }

    // something akin to mapgen fill_background.
    if( jo.has_string( "fill_ter" ) ) {
        fill_ter = ter_str_id( jo.get_string( "fill_ter" ) ).id();
    }

    if( jo.has_member( "rotation" ) ) {
        rotation = jmapgen_int( jo, "rotation" );
    }

    if( jo.has_member( "predecessor_mapgen" ) ) {
        predecessor_mapgen = oter_str_id( jo.get_string( "predecessor_mapgen" ) ).id();
    } else {
        predecessor_mapgen = oter_str_id::NULL_ID();
    }

    return fill_ter != t_null || predecessor_mapgen != oter_str_id::NULL_ID();
}

bool mapgen_function_json_nested::setup_internal( const JsonObject &jo )
{
    // Mandatory - nested mapgen must be explicitly sized
    if( jo.has_array( "mapgensize" ) ) {
        JsonArray jarr = jo.get_array( "mapgensize" );
        mapgensize = point( jarr.get_int( 0 ), jarr.get_int( 1 ) );
        if( mapgensize.x == 0 || mapgensize.x != mapgensize.y ) {
            // Non-square sizes not implemented yet
            jo.throw_error( "\"mapgensize\" must be an array of two identical, positive numbers" );
        }
        total_size = mapgensize;
    } else {
        jo.throw_error( "Nested mapgen must have \"mapgensize\" set" );
    }

    if( jo.has_member( "rotation" ) ) {
        rotation = jmapgen_int( jo, "rotation" );
    }

    // Nested mapgen is always halal because it can assume underlying map is.
    return true;
}

void mapgen_function_json::setup()
{
    setup_common();
}

void mapgen_function_json_nested::setup()
{
    setup_common();
}

void update_mapgen_function_json::setup()
{
    setup_common();
}

void mapgen_function_json::finalize_parameters()
{
    finalize_parameters_common();
}

void mapgen_function_json_nested::finalize_parameters()
{
    finalize_parameters_common();
}

void update_mapgen_function_json::finalize_parameters()
{
    finalize_parameters_common();
}

/*
 * Parse json, pre-calculating values for stuff, then cheerfully throw json away. Faster than regular mapf, in theory
 */
void mapgen_function_json_base::setup_common()
{
    if( is_ready ) {
        return;
    }
    if( !jsrcloc->path ) {
        debugmsg( "null json source location path" );
        return;
    }
    shared_ptr_fast<std::istream> stream = DynamicDataLoader::get_instance().get_cached_stream(
            *jsrcloc->path );
    JsonIn jsin( *stream, *jsrcloc );
    JsonObject jo = jsin.get_object();
    mapgen_defer::defer = false;
    if( !setup_common( jo ) ) {
        jsin.error( "format: no terrain map" );
    }
    if( mapgen_defer::defer ) {
        mapgen_defer::jsi.throw_error( mapgen_defer::message, mapgen_defer::member );
    } else {
        mapgen_defer::jsi = JsonObject();
    }
}

bool mapgen_function_json_base::setup_common( const JsonObject &jo )
{
    bool fallback_terrain_exists = setup_internal( jo );
    JsonArray parray;
    JsonArray sparray;
    JsonObject pjo;

    // just like mapf::basic_bind("stuff",blargle("foo", etc) ), only json input and faster when applying
    if( jo.has_array( "rows" ) ) {
        // TODO: forward correct 'src' parameter
        mapgen_palette palette = mapgen_palette::load_temp( jo,
                                 mod_management::get_default_core_content_pack().str(), "" );
        auto &keys_with_terrain = palette.keys_with_terrain;
        auto &format_placings = palette.format_placings;

        if( palette.keys_with_terrain.empty() && !fallback_terrain_exists ) {
            return false;
        }

        parameters = palette.get_parameters();

        // mandatory: mapgensize rows of mapgensize character lines, each of which must have a
        // matching key in "terrain", unless fill_ter is set
        // "rows:" [ "aaaajustlikeinmapgen.cpp", "this.must!be!exactly.24!", "and_must_match_terrain_", .... ]
        point expected_dim = mapgensize + m_offset;
        assert( expected_dim.x >= 0 );
        assert( expected_dim.y >= 0 );

        parray = jo.get_array( "rows" );
        if( static_cast<int>( parray.size() ) < expected_dim.y ) {
            parray.throw_error( string_format( "format: rows: must have at least %d rows, not %d",
                                               expected_dim.y, parray.size() ) );
        }
        if( static_cast<int>( parray.size() ) != total_size.y ) {
            parray.throw_error(
                string_format( "format: rows: must have %d rows, not %d; check mapgensize if applicable",
                               total_size.y, parray.size() ) );
        }
        for( int c = m_offset.y; c < expected_dim.y; c++ ) {
            const std::string row = parray.get_string( c );
            std::vector<map_key> row_keys;
            for( const std::string &key : utf8_display_split( row ) ) {
                row_keys.emplace_back( key );
            }
            if( row_keys.size() < static_cast<size_t>( expected_dim.x ) ) {
                parray.throw_error(
                    string_format( "  format: row %d must have at least %d columns, not %d",
                                   c + 1, expected_dim.x, row_keys.size() ) );
            }
            if( row_keys.size() != static_cast<size_t>( total_size.x ) ) {
                parray.throw_error(
                    string_format( "  format: row %d must have %d columns, not %d; check mapgensize if applicable",
                                   c + 1, total_size.x, row_keys.size() ) );
            }
            for( int i = m_offset.x; i < expected_dim.x; i++ ) {
                const point p = point( i, c ) - m_offset;
                const map_key key = row_keys[i];
                const auto iter_ter = keys_with_terrain.find( key );
                const auto fpi = format_placings.find( key );

                const bool has_terrain = iter_ter != keys_with_terrain.end();
                const bool has_placing = fpi != format_placings.end();

                if( !has_terrain && !fallback_terrain_exists ) {
                    parray.string_error(
                        string_format( "format: rows: row %d column %d: "
                                       "'%s' is not in 'terrain', and no 'fill_ter' is set!",
                                       c + 1, i + 1, key.str ), c, i + 1 );
                }
                if( !has_terrain && !has_placing &&
                    key.str != " " && key.str != "." ) {
                    try {
                        parray.string_error(
                            string_format( "format: rows: row %d column %d: "
                                           "'%s' has no terrain, furniture, or other definition",
                                           c + 1, i + 1, key.str ), c, i + 1 );
                    } catch( const JsonError &e ) {
                        debugmsg( "(json-error)\n%s", e.what() );
                    }
                }
                if( has_placing ) {
                    jmapgen_place where( p );
                    for( auto &what : fpi->second ) {
                        objects.add( where, what );
                    }
                }
            }
        }
        fallback_terrain_exists = true;
    }

    // No fill_ter? No format? GTFO.
    if( !fallback_terrain_exists ) {
        jo.throw_error(
            "Need one of 'fill_terrain' or 'predecessor_mapgen' or 'rows' + 'terrain'" );
        // TODO: write TFM.
    }

    if( jo.has_array( "set" ) ) {
        setup_setmap( jo.get_array( "set" ) );
    }

    // "add" is deprecated in favor of "place_item", but kept to support mods
    // which are not under our control.
    objects.load_objects<jmapgen_spawn_item>( jo, "add" );
    objects.load_objects<jmapgen_spawn_item>( jo, "place_item" );
    objects.load_objects<jmapgen_field>( jo, "place_fields" );
    objects.load_objects<jmapgen_npc>( jo, "place_npcs" );
    objects.load_objects<jmapgen_sign>( jo, "place_signs" );
    objects.load_objects<jmapgen_vending_machine>( jo, "place_vendingmachines" );
    objects.load_objects<jmapgen_toilet>( jo, "place_toilets" );
    objects.load_objects<jmapgen_liquid_item>( jo, "place_liquids" );
    objects.load_objects<jmapgen_gaspump>( jo, "place_gaspumps" );
    objects.load_objects<jmapgen_item_group>( jo, "place_items" );
    objects.load_objects<jmapgen_loot>( jo, "place_loot" );
    objects.load_objects<jmapgen_monster_group>( jo, "place_monsters" );
    objects.load_objects<jmapgen_vehicle>( jo, "place_vehicles" );
    objects.load_objects<jmapgen_trap>( jo, "place_traps" );
    objects.load_objects<jmapgen_furniture>( jo, "place_furniture" );
    objects.load_objects<jmapgen_terrain>( jo, "place_terrain" );
    objects.load_objects<jmapgen_monster>( jo, "place_monster" );
    objects.load_objects<jmapgen_make_rubble>( jo, "place_rubble" );
    objects.load_objects<jmapgen_computer>( jo, "place_computers" );
    objects.load_objects<jmapgen_nested>( jo, "place_nested" );
    objects.load_objects<jmapgen_graffiti>( jo, "place_graffiti" );
    objects.load_objects<jmapgen_translate>( jo, "translate_ter" );
    objects.load_objects<jmapgen_zone>( jo, "place_zones" );
    objects.load_objects<jmapgen_ter_furn_transform>( jo, "place_ter_furn_transforms" );
    // Needs to be last as it affects other placed items
    objects.load_objects<jmapgen_faction>( jo, "faction_owner" );

    objects.finalize();

    if( !mapgen_defer::defer ) {
        is_ready = true; // skip setup attempts from any additional pointers
    }
    return true;
}

void mapgen_function_json::check( const std::string &oter_name ) const
{
    check_common( oter_name );
}

void mapgen_function_json_nested::check( const std::string &oter_name ) const
{
    check_common( oter_name );
}

static bool check_furn( const furn_id &id, const std::string &context )
{
    const furn_t &furn = id.obj();
    if( furn.has_flag( "PLANT" ) ) {
        debugmsg( "json mapgen for %s specifies furniture %s, which has flag "
                  "PLANT.  Such furniture must be specified in a \"sealed_item\" special.",
                  context, furn.id.str() );
        // Only report once per mapgen object, otherwise the reports are
        // very repetitive
        return true;
    }
    return false;
}

void mapgen_function_json_base::check_common( const std::string &oter_name ) const
{
    for( const jmapgen_setmap &setmap : setmap_points ) {
        if( setmap.op != JMAPGEN_SETMAP_FURN &&
            setmap.op != JMAPGEN_SETMAP_LINE_FURN &&
            setmap.op != JMAPGEN_SETMAP_SQUARE_FURN ) {
            continue;
        }
        furn_id id( setmap.val.get() );
        if( check_furn( id, "oter " + oter_name ) ) {
            return;
        }
    }

    objects.check( oter_name, parameters );
}

void jmapgen_objects::finalize()
{
    std::stable_sort( objects.begin(), objects.end(),
    []( const jmapgen_obj & l, const jmapgen_obj & r ) {
        return l.second->phase() < r.second->phase();
    } );
}

void jmapgen_objects::check( const std::string &oter_name,
                             const mapgen_parameters &parameters ) const
{
    for( const jmapgen_obj &obj : objects ) {
        obj.second->check( oter_name, parameters );
    }
}

void jmapgen_objects::merge_parameters_into( mapgen_parameters &params,
        const std::string &outer_context ) const
{
    for( const jmapgen_obj &obj : objects ) {
        obj.second->merge_parameters_into( params, outer_context );
    }
}

/////////////////////////////////////////////////////////////////////////////////
///// 3 - mapgen (gameplay)
///// stuff below is the actual in-game map generation (ill)logic

/*
 * (set|line|square)_(ter|furn|trap|radiation); simple (x, y, int) or (x1,y1,x2,y2, int) functions
 * TODO: optimize, though gcc -O2 optimizes enough that splitting the switch has no effect
 */
bool jmapgen_setmap::apply( const mapgendata &dat, const point &offset ) const
{
    if( chance != 1 && !one_in( chance ) ) {
        return true;
    }

    const auto get = []( const jmapgen_int & v, int offset ) {
        return v.get() + offset;
    };
    const auto x_get = std::bind( get, x, offset.x );
    const auto y_get = std::bind( get, y, offset.y );
    const auto x2_get = std::bind( get, x2, offset.x );
    const auto y2_get = std::bind( get, y2, offset.y );

    map &m = dat.m;
    const int trepeat = repeat.get();
    for( int i = 0; i < trepeat; i++ ) {
        switch( op ) {
            case JMAPGEN_SETMAP_TER: {
                // TODO: the ter_id should be stored separately and not be wrapped in an jmapgen_int
                m.ter_set( point( x_get(), y_get() ), ter_id( val.get() ) );
            }
            break;
            case JMAPGEN_SETMAP_FURN: {
                // TODO: the furn_id should be stored separately and not be wrapped in an jmapgen_int
                m.furn_set( point( x_get(), y_get() ), furn_id( val.get() ) );
            }
            break;
            case JMAPGEN_SETMAP_TRAP: {
                // TODO: the trap_id should be stored separately and not be wrapped in an jmapgen_int
                mtrap_set( &m, point( x_get(), y_get() ), trap_id( val.get() ) );
            }
            break;
            case JMAPGEN_SETMAP_RADIATION: {
                m.set_radiation( point( x_get(), y_get() ), val.get() );
            }
            break;
            case JMAPGEN_SETMAP_BASH: {
                m.bash( tripoint( x_get(), y_get(), m.get_abs_sub().z ), 9999 );
            }
            break;

            case JMAPGEN_SETMAP_LINE_TER: {
                // TODO: the ter_id should be stored separately and not be wrapped in an jmapgen_int
                m.draw_line_ter( ter_id( val.get() ), point( x_get(), y_get() ), point( x2_get(), y2_get() ) );
            }
            break;
            case JMAPGEN_SETMAP_LINE_FURN: {
                // TODO: the furn_id should be stored separately and not be wrapped in an jmapgen_int
                m.draw_line_furn( furn_id( val.get() ), point( x_get(), y_get() ), point( x2_get(), y2_get() ) );
            }
            break;
            case JMAPGEN_SETMAP_LINE_TRAP: {
                const std::vector<point> line = line_to( point( x_get(), y_get() ), point( x2_get(), y2_get() ),
                                                0 );
                for( auto &i : line ) {
                    // TODO: the trap_id should be stored separately and not be wrapped in an jmapgen_int
                    mtrap_set( &m, i, trap_id( val.get() ) );
                }
            }
            break;
            case JMAPGEN_SETMAP_LINE_RADIATION: {
                const std::vector<point> line = line_to( point( x_get(), y_get() ), point( x2_get(), y2_get() ),
                                                0 );
                for( auto &i : line ) {
                    m.set_radiation( i, val.get() );
                }
            }
            break;
            case JMAPGEN_SETMAP_SQUARE_TER: {
                // TODO: the ter_id should be stored separately and not be wrapped in an jmapgen_int
                m.draw_square_ter( ter_id( val.get() ), point( x_get(), y_get() ), point( x2_get(), y2_get() ) );
            }
            break;
            case JMAPGEN_SETMAP_SQUARE_FURN: {
                // TODO: the furn_id should be stored separately and not be wrapped in an jmapgen_int
                m.draw_square_furn( furn_id( val.get() ), point( x_get(), y_get() ), point( x2_get(), y2_get() ) );
            }
            break;
            case JMAPGEN_SETMAP_SQUARE_TRAP: {
                const point c{ x_get(), y_get() };
                const point c2{ x2_get(), y2_get() };
                for( int tx = c.x; tx <= c2.x; tx++ ) {
                    for( int ty = c.y; ty <= c2.y; ty++ ) {
                        // TODO: the trap_id should be stored separately and not be wrapped in an jmapgen_int
                        mtrap_set( &m, point( tx, ty ), trap_id( val.get() ) );
                    }
                }
            }
            break;
            case JMAPGEN_SETMAP_SQUARE_RADIATION: {
                const int cx = x_get();
                const int cy = y_get();
                const int cx2 = x2_get();
                const int cy2 = y2_get();
                for( int tx = cx; tx <= cx2; tx++ ) {
                    for( int ty = cy; ty <= cy2; ty++ ) {
                        m.set_radiation( point( tx, ty ), val.get() );
                    }
                }
            }
            break;

            default:
                //Suppress warnings
                break;
        }
    }
    return true;
}

bool jmapgen_setmap::has_vehicle_collision( const mapgendata &dat, const point &offset ) const
{
    const auto get = []( const jmapgen_int & v, int v_offset ) {
        return v.get() + v_offset;
    };
    const auto x_get = std::bind( get, x, offset.x );
    const auto y_get = std::bind( get, y, offset.y );
    const auto x2_get = std::bind( get, x2, offset.x );
    const auto y2_get = std::bind( get, y2, offset.y );
    const tripoint start = tripoint( x_get(), y_get(), 0 );
    tripoint end = start;
    switch( op ) {
        case JMAPGEN_SETMAP_TER:
        case JMAPGEN_SETMAP_FURN:
        case JMAPGEN_SETMAP_TRAP:
            break;
        /* lines and squares are the same thing for this purpose */
        case JMAPGEN_SETMAP_LINE_TER:
        case JMAPGEN_SETMAP_LINE_FURN:
        case JMAPGEN_SETMAP_LINE_TRAP:
        case JMAPGEN_SETMAP_SQUARE_TER:
        case JMAPGEN_SETMAP_SQUARE_FURN:
        case JMAPGEN_SETMAP_SQUARE_TRAP:
            end.x = x2_get();
            end.y = y2_get();
            break;
        /* if it's not a terrain, furniture, or trap, it can't collide */
        default:
            return false;
    }
    for( const tripoint &p : dat.m.points_in_rectangle( start, end ) ) {
        if( dat.m.veh_at( p ) ) {
            return true;
        }
    }
    return false;
}

bool mapgen_function_json_base::has_vehicle_collision(
    const mapgendata &dat, const point &offset ) const
{
    for( const jmapgen_setmap &elem : setmap_points ) {
        if( elem.has_vehicle_collision( dat, offset ) ) {
            return true;
        }
    }

    return objects.has_vehicle_collision( dat, offset );
}

/*
 * Apply mapgen as per a derived-from-json recipe; in theory fast, but not very versatile
 */
void mapgen_function_json::generate( mapgendata &md )
{
    map *const m = &md.m;
    if( fill_ter != t_null ) {
        m->draw_fill_background( fill_ter );
    }
    const oter_t &ter = *md.terrain_type();

    if( predecessor_mapgen != oter_str_id::NULL_ID() ) {
        mapgendata predecessor_mapgen_dat( md, predecessor_mapgen );
        run_mapgen_func( predecessor_mapgen.id().str(), predecessor_mapgen_dat );

        // Now we have to do some rotation shenanigans. We need to ensure that
        // our predecessor is not rotated out of alignment as part of rotating this location,
        // and there are actually two sources of rotation--the mapgen can rotate explicitly, and
        // the entire overmap terrain may be rotatable. To ensure we end up in the right rotation,
        // we basically have to initially reverse the rotation that we WILL do in the future so that
        // when we apply that rotation, our predecessor is back in its original state while this
        // location is rotated as desired.

        m->rotate( ( -rotation.get() + 4 ) % 4 );

        if( ter.is_rotatable() || ter.is_linear() ) {
            m->rotate( ( -ter.get_rotation() + 4 ) % 4 );
        }
    }

    mapgendata md_with_params( md, get_args( md, mapgen_parameter_scope::omt ) );

    for( auto &elem : setmap_points ) {
        elem.apply( md_with_params, point_zero );
    }

    objects.apply( md_with_params, point_zero );

    resolve_regional_terrain_and_furniture( md_with_params );

    m->rotate( rotation.get() );

    if( ter.is_rotatable() || ter.is_linear() ) {
        m->rotate( ter.get_rotation() );
    }
}

mapgen_parameters mapgen_function_json::get_mapgen_params( mapgen_parameter_scope scope ) const
{
    return parameters.params_for_scope( scope );
}

void mapgen_function_json_nested::nest( const mapgendata &md, const point &offset ) const
{
    // TODO: Make rotation work for submaps, then pass this value into elem & objects apply.
    //int chosen_rotation = rotation.get() % 4;

    mapgendata md_with_params( md, get_args( md, mapgen_parameter_scope::nest ) );

    for( const jmapgen_setmap &elem : setmap_points ) {
        elem.apply( md_with_params, offset );
    }

    objects.apply( md_with_params, offset );

    resolve_regional_terrain_and_furniture( md_with_params );
}

/*
 * Apply mapgen as per a derived-from-json recipe; in theory fast, but not very versatile
 */
void jmapgen_objects::apply( const mapgendata &dat ) const
{
    for( auto &obj : objects ) {
        const auto &where = obj.first;
        const auto &what = *obj.second;
        // The user will only specify repeat once in JSON, but it may get loaded both
        // into the what and where in some cases--we just need the greater value of the two.
        const int repeat = std::max( where.repeat.get(), what.repeat.get() );
        for( int i = 0; i < repeat; i++ ) {
            what.apply( dat, where.x, where.y );
        }
    }
}

void jmapgen_objects::apply( const mapgendata &dat, const point &offset ) const
{
    if( offset == point_zero ) {
        // It's a bit faster
        apply( dat );
        return;
    }

    for( auto &obj : objects ) {
        auto where = obj.first;
        where.offset( -offset );

        const auto &what = *obj.second;
        // The user will only specify repeat once in JSON, but it may get loaded both
        // into the what and where in some cases--we just need the greater value of the two.
        const int repeat = std::max( where.repeat.get(), what.repeat.get() );
        for( int i = 0; i < repeat; i++ ) {
            what.apply( dat, where.x, where.y );
        }
    }
}

bool jmapgen_objects::has_vehicle_collision( const mapgendata &dat, const point &offset ) const
{
    for( auto &obj : objects ) {
        auto where = obj.first;
        where.offset( -offset );
        const auto &what = *obj.second;
        if( what.has_vehicle_collision( dat, point( where.x.get(), where.y.get() ) ) ) {
            return true;
        }
    }
    return false;
}

/////////////
void map::draw_map( mapgendata &dat )
{
    const oter_id &terrain_type = dat.terrain_type();
    const std::string function_key = terrain_type->get_mapgen_id();
    bool found = true;

    const bool generated = run_mapgen_func( function_key, dat );

    if( !generated ) {
        if( is_ot_match( "slimepit", terrain_type, ot_match_type::prefix ) ||
            is_ot_match( "slime_pit", terrain_type, ot_match_type::prefix ) ) {
            draw_slimepit( dat );
        } else if( is_ot_match( "office", terrain_type, ot_match_type::prefix ) ) {
            draw_office_tower( dat );
        } else if( is_ot_match( "temple", terrain_type, ot_match_type::prefix ) ) {
            draw_temple( dat );
        } else if( is_ot_match( "mine", terrain_type, ot_match_type::prefix ) ) {
            draw_mine( dat );
        } else if( is_ot_match( "lab", terrain_type, ot_match_type::contains ) ) {
            draw_lab( dat );
        } else {
            found = false;
        }
    }

    if( !found ) {
        // not one of the hardcoded ones!
        // load from JSON???
        debugmsg( "Error: tried to generate map for omtype %s, \"%s\" (id_mapgen %s)",
                  terrain_type.id().c_str(), terrain_type->get_name(), function_key.c_str() );
        fill_background( this, t_floor );
    }

    draw_connections( dat );
}

const int SOUTH_EDGE = 2 * SEEY - 1;
const int EAST_EDGE = 2 * SEEX  - 1;

void map::draw_office_tower( const mapgendata &dat )
{
    const oter_id &terrain_type = dat.terrain_type();
    const auto place_office_chairs = [&]() {
        int num_chairs = rng( 0, 6 );
        for( int i = 0; i < num_chairs; i++ ) {
            add_vehicle( vproto_id( "swivel_chair" ), point( rng( 6, 16 ), rng( 6, 16 ) ),
                         0_degrees, -1, -1, false );
        }
    };

    const auto ter_key = mapf::ter_bind( "E > < R # X G C , _ r V H 6 x % ^ . - | "
                                         "t + = D w T S e o h c d l s", t_elevator, t_stairs_down,
                                         t_stairs_up, t_railing, t_rock, t_door_metal_locked,
                                         t_door_glass_c, t_floor, t_pavement_y, t_pavement,
                                         t_floor, t_wall_glass, t_wall_glass, t_console,
                                         t_console_broken, t_shrub, t_floor, t_floor, t_wall,
                                         t_wall, t_floor, t_door_c, t_door_locked,
                                         t_door_locked_alarm, t_window, t_floor, t_floor, t_floor,
                                         t_floor, t_floor, t_floor, t_floor, t_floor, t_sidewalk );
    const auto fur_key = mapf::furn_bind( "E > < R # X G C , _ r V H 6 x % ^ . - | "
                                          "t + = D w T S e o h c d l s", f_null, f_null, f_null,
                                          f_null, f_null, f_null, f_null, f_crate_c, f_null,
                                          f_null, f_rack, f_null, f_null, f_null, f_null, f_null,
                                          f_indoor_plant, f_null, f_null, f_null, f_table, f_null,
                                          f_null, f_null, f_null, f_toilet, f_sink, f_fridge,
                                          f_bookcase, f_chair, f_counter, f_desk,  f_locker,
                                          f_null );
    const auto b_ter_key = mapf::ter_bind( "E s > < R # X G C , . r V H 6 x % ^ _ - | "
                                           "t + = D w T S e o h c d l", t_elevator, t_rock,
                                           t_stairs_down, t_stairs_up, t_railing, t_floor,
                                           t_door_metal_locked, t_door_glass_c, t_floor,
                                           t_pavement_y, t_pavement, t_floor, t_wall_glass,
                                           t_wall_glass, t_console, t_console_broken, t_shrub,
                                           t_floor, t_floor, t_wall, t_wall, t_floor, t_door_c,
                                           t_door_locked, t_door_locked_alarm, t_window, t_floor,
                                           t_sidewalk, t_floor, t_floor, t_floor, t_floor,
                                           t_floor, t_floor );
    const auto b_fur_key = mapf::furn_bind( "E s > < R # X G C , . r V H 6 x % ^ _ - | "
                                            "t + = D w T S e o h c d l", f_null, f_null, f_null,
                                            f_null, f_null, f_bench, f_null, f_null, f_crate_c,
                                            f_null, f_null, f_rack, f_null, f_null, f_null,
                                            f_null, f_null, f_indoor_plant, f_null, f_null,
                                            f_null, f_table, f_null, f_null, f_null, f_null,
                                            f_toilet, f_null,  f_fridge, f_bookcase, f_chair,
                                            f_counter, f_desk,  f_locker );

    if( terrain_type == "office_tower_1_entrance" ) {
        dat.fill_groundcover();
        mapf::formatted_set_simple( this, point_zero,
                                    "ss%|....+...|...|EEED...\n"
                                    "ss%|----|...|...|EEx|...\n"
                                    "ss%Vcdc^|...|-+-|---|...\n"
                                    "ss%Vch..+...............\n"
                                    "ss%V....|...............\n"
                                    "ss%|----|-|-+--ccc--|...\n"
                                    "ss%|..C..C|.....h..r|-+-\n"
                                    "sss=......+..h.....r|...\n"
                                    "ss%|r..CC.|.ddd....r|T.S\n"
                                    "ss%|------|---------|---\n"
                                    "ss%|####################\n"
                                    "ss%|#|------||------|###\n"
                                    "ss%|#|......||......|###\n"
                                    "ss%|||......||......|###\n"
                                    "ss%||x......||......||##\n"
                                    "ss%|||......||......x|##\n"
                                    "ss%|#|......||......||##\n"
                                    "ss%|#|......||......|###\n"
                                    "ss%|#|XXXXXX||XXXXXX|###\n"
                                    "ss%|-|__,,__||__,,__|---\n"
                                    "ss%% x_,,,,_  __,,__  %%\n"
                                    "ss    __,,__  _,,,,_    \n"
                                    "ssssss__,,__ss__,,__ssss\n"
                                    "ssssss______ss______ssss\n", ter_key, fur_key );
        place_items( item_group_id( "office" ), 75, point( 4, 2 ), point( 6, 2 ), false,
                     calendar::start_of_cataclysm );
        place_items( item_group_id( "office" ), 75, point( 19, 6 ), point( 19, 6 ), false,
                     calendar::start_of_cataclysm );
        place_items( item_group_id( "office" ), 75, point( 12, 8 ), point( 14, 8 ), false,
                     calendar::start_of_cataclysm );
        if( dat.monster_density() > 1 ) {
            place_spawns( GROUP_ZOMBIE, 2, point_zero, point( 12, 3 ), dat.monster_density() );
        } else {
            place_spawns( GROUP_PLAIN, 2, point( 15, 1 ), point( 22, 7 ), 1, true );
            place_spawns( GROUP_PLAIN, 2, point( 15, 1 ), point( 22, 7 ), 0.15 );
            place_spawns( GROUP_ZOMBIE_COP, 2, point( 10, 10 ), point( 14, 10 ), 0.1 );
        }
        place_office_chairs();

        if( dat.north() == "office_tower_1" && dat.west() == "office_tower_1" ) {
            rotate( 3 );
        } else if( dat.north() == "office_tower_1" && dat.east() == "office_tower_1" ) {
            rotate( 0 );
        } else if( dat.south() == "office_tower_1" && dat.east() == "office_tower_1" ) {
            rotate( 1 );
        } else if( dat.west() == "office_tower_1" && dat.south() == "office_tower_1" ) {
            rotate( 2 );
        }
    } else if( terrain_type == "office_tower_1" ) {
        // Init to grass & dirt;
        dat.fill_groundcover();
        if( ( dat.south() == "office_tower_1_entrance" && dat.east() == "office_tower_1" ) ||
            ( dat.north() == "office_tower_1" && dat.east() == "office_tower_1_entrance" ) ||
            ( dat.west() == "office_tower_1" && dat.north() == "office_tower_1_entrance" ) ||
            ( dat.south() == "office_tower_1" && dat.west() == "office_tower_1_entrance" ) ) {
            mapf::formatted_set_simple( this, point_zero,
                                        " ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "ss                      \n"
                                        "ss%%%%%%%%%%%%%%%%%%%%%%\n"
                                        "ss%|-HH-|-HH-|-HH-|HH|--\n"
                                        "ss%Vdcxl|dxdl|lddx|..|.S\n"
                                        "ss%Vdh..|dh..|..hd|..+..\n"
                                        "ss%|-..-|-..-|-..-|..|--\n"
                                        "ss%V.................|.T\n"
                                        "ss%V.................|..\n"
                                        "ss%|-..-|-..-|-..-|..|--\n"
                                        "ss%V.h..|..hd|..hd|..|..\n"
                                        "ss%Vdxdl|^dxd|.xdd|..G..\n"
                                        "ss%|----|----|----|..G..\n"
                                        "ss%|llll|..htth......|..\n"
                                        "ss%V.................|..\n"
                                        "ss%V.ddd..........|+-|..\n"
                                        "ss%|..hd|.hh.ceocc|.l|..\n"
                                        "ss%|----|---------|--|..\n"
                                        "ss%Vcdcl|...............\n"
                                        "ss%V.h..+...............\n"
                                        "ss%V...^|...|---|---|...\n"
                                        "ss%|----|...|.R>|EEE|...\n"
                                        "ss%|rrrr|...|.R.|EEED...\n", ter_key, fur_key );
            if( dat.monster_density() > 1 ) {
                place_spawns( GROUP_ZOMBIE, 2, point_zero, point( 2, 8 ), dat.monster_density() );
            } else {
                place_spawns( GROUP_PLAIN, 1, point( 5, 7 ), point( 15, 20 ), 0.1 );
            }
            place_items( item_group_id( "office" ), 75, point( 4, 23 ), point( 7, 23 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 75, point( 4, 19 ), point( 7, 19 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 75, point( 4, 14 ), point( 7, 14 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 75, point( 5, 16 ), point( 7, 16 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "fridge" ), 80, point( 14, 17 ), point( 14, 17 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cleaning" ), 75, point( 19, 17 ), point( 20, 17 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 6, 12 ), point( 7, 12 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 12, 11 ), point( 12, 12 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 16, 11 ), point( 17, 12 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 4, 5 ), point( 5, 5 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 11, 5 ), point( 12, 5 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 14, 5 ), point( 16, 5 ), false,
                         calendar::start_of_cataclysm );
            place_office_chairs();

            if( dat.west() == "office_tower_1_entrance" ) {
                rotate( 1 );
            }
            if( dat.north() == "office_tower_1_entrance" ) {
                rotate( 2 );
            }
            if( dat.east() == "office_tower_1_entrance" ) {
                rotate( 3 );
            }
        } else if( ( dat.west() == "office_tower_1_entrance" && dat.north() == "office_tower_1" ) ||
                   ( dat.north() == "office_tower_1_entrance" && dat.east() == "office_tower_1" ) ||
                   ( dat.west() == "office_tower_1" && dat.south() == "office_tower_1_entrance" ) ||
                   ( dat.south() == "office_tower_1" && dat.east() == "office_tower_1_entrance" ) ) {
            mapf::formatted_set_simple( this, point_zero,
                                        "...DEEE|...|..|-----|%ss\n"
                                        "...|EEE|...|..|^...lV%ss\n"
                                        "...|---|-+-|......hdV%ss\n"
                                        "...........G..|..dddV%ss\n"
                                        "...........G..|-----|%ss\n"
                                        ".......|---|..|...ddV%ss\n"
                                        "|+-|...|...+......hdV%ss\n"
                                        "|.l|...|rr.|.^|l...dV%ss\n"
                                        "|--|...|---|--|-----|%ss\n"
                                        "|...........c.......V%ss\n"
                                        "|.......cxh.c.#####.Vsss\n"
                                        "|.......ccccc.......Gsss\n"
                                        "|...................Gsss\n"
                                        "|...................Vsss\n"
                                        "|#..................Gsss\n"
                                        "|#..................Gsss\n"
                                        "|#..................Vsss\n"
                                        "|#............#####.V%ss\n"
                                        "|...................|%ss\n"
                                        "--HHHHHGGHHGGHHHHH--|%ss\n"
                                        "%%%%% ssssssss %%%%%%%ss\n"
                                        "      ssssssss        ss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n", ter_key, fur_key );
            place_items( item_group_id( "office" ), 75, point( 19, 1 ), point( 19, 3 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 75, point( 17, 3 ), point( 18, 3 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 90, point( 8, 7 ), point( 9, 7 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 19, 5 ), point( 19, 7 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cleaning" ), 80, point( 1, 7 ), point( 2, 7 ), false,
                         calendar::start_of_cataclysm );
            if( dat.monster_density() > 1 ) {
                place_spawns( GROUP_ZOMBIE, 2, point_zero, point( 14, 10 ), dat.monster_density() );
            } else {
                place_spawns( GROUP_PLAIN, 1, point( 10, 10 ), point( 14, 10 ), 0.15 );
                place_spawns( GROUP_ZOMBIE_COP, 2, point( 10, 10 ), point( 14, 10 ), 0.1 );
            }
            place_office_chairs();

            if( dat.north() == "office_tower_1_entrance" ) {
                rotate( 1 );
            }
            if( dat.east() == "office_tower_1_entrance" ) {
                rotate( 2 );
            }
            if( dat.south() == "office_tower_1_entrance" ) {
                rotate( 3 );
            }
        } else {
            mapf::formatted_set_simple( this, point_zero,
                                        "ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "                      ss\n"
                                        "%%%%%%%%%%%%%%%%%%%%%%ss\n"
                                        "--|---|--HHHH-HHHH--|%ss\n"
                                        ".T|..l|............^|%ss\n"
                                        "..|-+-|...hhhhhhh...V%ss\n"
                                        "--|...G...ttttttt...V%ss\n"
                                        ".S|...G...ttttttt...V%ss\n"
                                        "..+...|...hhhhhhh...V%ss\n"
                                        "--|...|.............|%ss\n"
                                        "..|...|-------------|%ss\n"
                                        "..G....|l.......dxd^|%ss\n"
                                        "..G....G...h....dh..V%ss\n"
                                        "..|....|............V%ss\n"
                                        "..|....|------|llccc|%ss\n"
                                        "..|...........|-----|%ss\n"
                                        "..|...........|...ddV%ss\n"
                                        "..|----|---|......hdV%ss\n"
                                        ".......+...|..|l...dV%ss\n"
                                        ".......|rrr|..|-----|%ss\n"
                                        "...|---|---|..|l.dddV%ss\n"
                                        "...|xEE|.R>|......hdV%ss\n"
                                        "...DEEE|.R.|..|.....V%ss\n", ter_key, fur_key );
            spawn_item( point( 18, 15 ), "record_accounting" );
            place_items( item_group_id( "cleaning" ), 75, point( 3, 5 ), point( 5, 5 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 75, point( 10, 7 ), point( 16, 8 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 15, 15 ), point( 19, 15 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 16, 12 ), point( 16, 13 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cubical_office" ), 75, point( 17, 19 ), point( 19, 19 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 75, point( 17, 21 ), point( 19, 21 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "office" ), 75, point( 16, 11 ), point( 17, 12 ), false,
                         calendar::start_of_cataclysm );
            place_items( item_group_id( "cleaning" ), 75, point( 8, 20 ), point( 10, 20 ), false,
                         calendar::start_of_cataclysm );
            if( dat.monster_density() > 1 ) {
                place_spawns( GROUP_ZOMBIE, 2, point_zero, point( 9, 15 ), dat.monster_density() );
            } else {
                place_spawns( GROUP_PLAIN, 1, point_zero, point( 9, 15 ), 0.1 );
            }
            place_office_chairs();

            if( dat.west() == "office_tower_1" && dat.north() == "office_tower_1" ) {
                rotate( 1 );
            } else if( dat.east() == "office_tower_1" && dat.north() == "office_tower_1" ) {
                rotate( 2 );
            } else if( dat.east() == "office_tower_1" && dat.south() == "office_tower_1" ) {
                rotate( 3 );
            }
        }
    } else if( terrain_type == "office_tower_b_entrance" ) {
        dat.fill_groundcover();
        mapf::formatted_set_simple( this, point_zero,
                                    "sss|........|...|EEED___\n"
                                    "sss|........|...|EEx|___\n"
                                    "sss|........|-+-|---|HHG\n"
                                    "sss|....................\n"
                                    "sss|....................\n"
                                    "sss|....................\n"
                                    "sss|....................\n"
                                    "sss|....,,......,,......\n"
                                    "sss|...,,,,.....,,......\n"
                                    "sss|....,,.....,,,,..xS.\n"
                                    "sss|....,,......,,...SS.\n"
                                    "sss|-|XXXXXX||XXXXXX|---\n"
                                    "sss|s|EEEEEE||EEEEEE|sss\n"
                                    "sss|||EEEEEE||EEEEEE|sss\n"
                                    "sss||xEEEEEE||EEEEEE||ss\n"
                                    "sss|||EEEEEE||EEEEEEx|ss\n"
                                    "sss|s|EEEEEE||EEEEEE||ss\n"
                                    "sss|s|EEEEEE||EEEEEE|sss\n"
                                    "sss|s|------||------|sss\n"
                                    "sss|--------------------\n"
                                    "ssssssssssssssssssssssss\n"
                                    "ssssssssssssssssssssssss\n"
                                    "ssssssssssssssssssssssss\n"
                                    "ssssssssssssssssssssssss\n", ter_key, fur_key );
        if( dat.monster_density() > 1 ) {
            place_spawns( GROUP_ZOMBIE, 2, point_zero, point( EAST_EDGE, SOUTH_EDGE ), dat.monster_density() );
        } else {
            place_spawns( GROUP_PLAIN, 1, point_zero, point( EAST_EDGE, SOUTH_EDGE ), 0.1 );
        }
        if( dat.north() == "office_tower_b" && dat.west() == "office_tower_b" ) {
            rotate( 3 );
        } else if( dat.north() == "office_tower_b" && dat.east() == "office_tower_b" ) {
            rotate( 0 );
        } else if( dat.south() == "office_tower_b" && dat.east() == "office_tower_b" ) {
            rotate( 1 );
        } else if( dat.west() == "office_tower_b" && dat.south() == "office_tower_b" ) {
            rotate( 2 );
        }
    } else if( terrain_type == "office_tower_b" ) {
        // Init to grass & dirt;
        dat.fill_groundcover();
        if( ( dat.south() == "office_tower_b_entrance" && dat.east() == "office_tower_b" ) ||
            ( dat.north() == "office_tower_b" && dat.east() == "office_tower_b_entrance" ) ||
            ( dat.west() == "office_tower_b" && dat.north() == "office_tower_b_entrance" ) ||
            ( dat.south() == "office_tower_b" && dat.west() == "office_tower_b_entrance" ) ) {
            mapf::formatted_set_simple( this, point_zero,
                                        "ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "sss|--------------------\n"
                                        "sss|,.....,.....,.....,S\n"
                                        "sss|,.....,.....,.....,S\n"
                                        "sss|,.....,.....,.....,S\n"
                                        "sss|,.....,.....,.....,S\n"
                                        "sss|,.....,.....,.....,S\n"
                                        "sss|,.....,.....,.....,S\n"
                                        "sss|....................\n"
                                        "sss|....................\n"
                                        "sss|....................\n"
                                        "sss|....................\n"
                                        "sss|....................\n"
                                        "sss|....................\n"
                                        "sss|...,,...,....,....,S\n"
                                        "sss|..,,,,..,....,....,S\n"
                                        "sss|...,,...,....,....,S\n"
                                        "sss|...,,...,....,....,S\n"
                                        "sss|........,....,....,S\n"
                                        "sss|........,....,....,S\n"
                                        "sss|........|---|---|HHG\n"
                                        "sss|........|.R<|EEE|___\n"
                                        "sss|........|.R.|EEED___\n", b_ter_key, b_fur_key );
            if( dat.monster_density() > 1 ) {
                place_spawns( GROUP_ZOMBIE, 2, point_zero, point( EAST_EDGE, SOUTH_EDGE ), dat.monster_density() );
            } else {
                place_spawns( GROUP_PLAIN, 1, point_zero, point( EAST_EDGE, SOUTH_EDGE ), 0.1 );
            }
            if( dat.west() == "office_tower_b_entrance" ) {
                rotate( 1 );
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 17, 7 ), 180_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "motorcycle" ), point( 17, 13 ), 180_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    if( one_in( 3 ) ) {
                        add_vehicle( vproto_id( "fire_truck" ), point( 6, 13 ), 0_degrees );
                    } else {
                        add_vehicle( vproto_id( "pickup" ), point( 17, 19 ), 180_degrees );
                    }
                }
            } else if( dat.north() == "office_tower_b_entrance" ) {
                rotate( 2 );
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 10, 17 ), 270_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "motorcycle" ), point( 4, 18 ), 270_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    if( one_in( 3 ) ) {
                        add_vehicle( vproto_id( "fire_truck" ), point( 6, 13 ), 0_degrees );
                    } else {
                        add_vehicle( vproto_id( "pickup" ), point( 16, 17 ), 270_degrees );
                    }
                }
            } else if( dat.east() == "office_tower_b_entrance" ) {
                rotate( 3 );
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 6, 4 ), 0_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "motorcycle" ), point( 6, 10 ), 180_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "pickup" ), point( 6, 16 ), 0_degrees );
                }

            } else {
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "pickup" ), point( 7, 6 ), 90_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 14, 6 ), 90_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "motorcycle" ), point( 19, 6 ), 90_degrees );
                }
            }
        } else if( ( dat.west() == "office_tower_b_entrance" && dat.north() == "office_tower_b" ) ||
                   ( dat.north() == "office_tower_b_entrance" && dat.east() == "office_tower_b" ) ||
                   ( dat.west() == "office_tower_b" && dat.south() == "office_tower_b_entrance" ) ||
                   ( dat.south() == "office_tower_b" && dat.east() == "office_tower_b_entrance" ) ) {
            mapf::formatted_set_simple( this, point_zero,
                                        "___DEEE|...|...,,...|sss\n"
                                        "___|EEE|...|..,,,,..|sss\n"
                                        "GHH|---|-+-|...,,...|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "|...................|sss\n"
                                        "|...................|sss\n"
                                        "|,.....,.....,.....,|sss\n"
                                        "|,.....,.....,.....,|sss\n"
                                        "|,.....,.....,.....,|sss\n"
                                        "|,.....,.....,.....,|sss\n"
                                        "|,.....,.....,.....,|sss\n"
                                        "|,.....,.....,.....,|sss\n"
                                        "|-------------------|sss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n", b_ter_key, b_fur_key );
            if( dat.monster_density() > 1 ) {
                place_spawns( GROUP_ZOMBIE, 2, point_zero, point( EAST_EDGE, SOUTH_EDGE ), dat.monster_density() );
            } else {
                place_spawns( GROUP_PLAIN, 1, point_zero, point( EAST_EDGE, SOUTH_EDGE ), 0.1 );
            }
            if( dat.north() == "office_tower_b_entrance" ) {
                rotate( 1 );
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 8, 15 ), 0_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "pickup" ), point( 7, 10 ), 180_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "beetle" ), point( 7, 3 ), 0_degrees );
                }
            } else if( dat.east() == "office_tower_b_entrance" ) {
                rotate( 2 );
                if( x_in_y( 1, 5 ) ) {
                    if( one_in( 3 ) ) {
                        add_vehicle( vproto_id( "fire_truck" ), point( 6, 13 ), 0_degrees );
                    } else {
                        add_vehicle( vproto_id( "pickup" ), point( 7, 7 ), 270_degrees );
                    }
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 13, 8 ), 90_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "beetle" ), point( 20, 7 ), 90_degrees );
                }
            } else if( dat.south() == "office_tower_b_entrance" ) {
                rotate( 3 );
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "pickup" ), point( 16, 7 ), 0_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 15, 13 ), 180_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "beetle" ), point( 15, 20 ), 180_degrees );
                }
            } else {
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "pickup" ), point( 16, 16 ), 90_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 9, 15 ), 270_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "beetle" ), point( 4, 16 ), 270_degrees );
                }
            }
        } else {
            mapf::formatted_set_simple( this, point_zero,
                                        "ssssssssssssssssssssssss\n"
                                        "ssssssssssssssssssssssss\n"
                                        "--------------------|sss\n"
                                        "S,.....,.....,.....,|sss\n"
                                        "S,.....,.....,.....,|sss\n"
                                        "S,.....,.....,.....,|sss\n"
                                        "S,.....,.....,.....,|sss\n"
                                        "S,.....,.....,.....,|sss\n"
                                        "S,.....,.....,.....,|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "....................|sss\n"
                                        "S,....,....,........|sss\n"
                                        "S,....,....,........|sss\n"
                                        "S,....,....,........|sss\n"
                                        "S,....,....,........|sss\n"
                                        "S,....,....,........|sss\n"
                                        "S,....,....,........|sss\n"
                                        "GHH|---|---|........|sss\n"
                                        "___|xEE|.R<|........|sss\n"
                                        "___DEEE|.R.|...,,...|sss\n", b_ter_key, b_fur_key );
            if( dat.monster_density() > 1 ) {
                place_spawns( GROUP_ZOMBIE, 2, point_zero, point( EAST_EDGE, SOUTH_EDGE ), dat.monster_density() );
            } else {
                place_spawns( GROUP_PLAIN, 1, point_zero, point( EAST_EDGE, SOUTH_EDGE ), 0.1 );
            }
            if( dat.west() == "office_tower_b" && dat.north() == "office_tower_b" ) {
                rotate( 1 );
                if( x_in_y( 1, 5 ) ) {
                    if( one_in( 3 ) ) {
                        add_vehicle( vproto_id( "cube_van" ), point( 17, 4 ), 180_degrees );
                    } else {
                        add_vehicle( vproto_id( "cube_van_cheap" ), point( 17, 4 ), 180_degrees );
                    }
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "pickup" ), point( 17, 10 ), 180_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 17, 17 ), 180_degrees );
                }
            } else if( dat.east() == "office_tower_b" && dat.north() == "office_tower_b" ) {
                rotate( 2 );
                if( x_in_y( 1, 5 ) ) {
                    if( one_in( 3 ) ) {
                        add_vehicle( vproto_id( "cube_van" ), point( 6, 17 ), 270_degrees );
                    } else {
                        add_vehicle( vproto_id( "cube_van_cheap" ), point( 6, 17 ), 270_degrees );
                    }
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "pickup" ), point( 12, 17 ), 270_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "fire_truck" ), point( 18, 17 ), 270_degrees );
                }
            } else if( dat.east() == "office_tower_b" && dat.south() == "office_tower_b" ) {
                rotate( 3 );
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "cube_van_cheap" ), point( 6, 6 ), 0_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    if( one_in( 3 ) ) {
                        add_vehicle( vproto_id( "fire_truck" ), point( 6, 13 ), 0_degrees );
                    } else {
                        add_vehicle( vproto_id( "pickup" ), point( 6, 13 ), 0_degrees );
                    }
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 5, 19 ), 180_degrees );
                }
            } else {
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "flatbed_truck" ), point( 16, 6 ), 90_degrees );
                }
                if( x_in_y( 1, 5 ) ) {
                    add_vehicle( vproto_id( "cube_van_cheap" ), point( 10, 6 ), 90_degrees );
                }
                if( x_in_y( 1, 3 ) ) {
                    add_vehicle( vproto_id( "car" ), point( 4, 6 ), 90_degrees );
                }
            }
        }
    }
}

void map::draw_lab( mapgendata &dat )
{
    const oter_id &terrain_type = dat.terrain_type();
    // To distinguish between types of labs
    bool ice_lab = true;
    bool central_lab = false;
    bool tower_lab = false;
    bool ants_lab = false;

    point p2;

    int lw = 0;
    int rw = 0;
    int tw = 0;
    int bw = 0;

    if( terrain_type == "lab" || terrain_type == "lab_stairs" || terrain_type == "lab_core" ||
        terrain_type == "ants_lab" || terrain_type == "ants_lab_stairs" ||
        terrain_type == "ice_lab" || terrain_type == "ice_lab_stairs" ||
        terrain_type == "ice_lab_core" ||
        terrain_type == "central_lab" || terrain_type == "central_lab_stairs" ||
        terrain_type == "central_lab_core" ||
        terrain_type == "tower_lab" || terrain_type == "tower_lab_stairs" ) {

        ice_lab = is_ot_match( "ice_lab", terrain_type, ot_match_type::prefix );
        central_lab = is_ot_match( "central_lab", terrain_type, ot_match_type::prefix );
        tower_lab = is_ot_match( "tower_lab", terrain_type, ot_match_type::prefix );
        ants_lab = is_ot_match( "ants", dat.north(), ot_match_type::contains ) ||
                   is_ot_match( "ants", dat.east(), ot_match_type::contains ) ||
                   is_ot_match( "ants", dat.south(), ot_match_type::contains ) ||
                   is_ot_match( "ants", dat.west(), ot_match_type::contains );

        if( ice_lab ) {
            int temperature = -20 + 30 * ( dat.zlevel() );
            set_temperature( p2, temperature );
            set_temperature( p2 + point( SEEX, 0 ), temperature );
            set_temperature( p2 + point( 0, SEEY ), temperature );
            set_temperature( p2 + point( SEEX, SEEY ), temperature );
        }

        // Check for adjacent sewers; used below
        tw = 0;
        rw = 0;
        bw = 0;
        lw = 0;
        if( is_ot_match( "sewer", dat.north(), ot_match_type::type ) && connects_to( dat.north(), 2 ) ) {
            tw = SOUTH_EDGE + 1;
        }
        if( is_ot_match( "sewer", dat.east(), ot_match_type::type ) && connects_to( dat.east(), 3 ) ) {
            rw = EAST_EDGE + 1;
        }
        if( is_ot_match( "sewer", dat.south(), ot_match_type::type ) && connects_to( dat.south(), 0 ) ) {
            bw = SOUTH_EDGE + 1;
        }
        if( is_ot_match( "sewer", dat.west(), ot_match_type::type ) && connects_to( dat.west(), 1 ) ) {
            lw = EAST_EDGE + 1;
        }
        if( dat.zlevel() == 0 ) { // We're on ground level
            for( int i = 0; i < SEEX * 2; i++ ) {
                for( int j = 0; j < SEEY * 2; j++ ) {
                    if( i <= 1 || i >= SEEX * 2 - 2 ||
                        ( j > 1 && j < SEEY * 2 - 2 && ( i == SEEX - 2 || i == SEEX + 1 ) ) ) {
                        ter_set( point( i, j ), t_concrete_wall );
                    } else if( j <= 1 || j >= SEEY * 2 - 2 ) {
                        ter_set( point( i, j ), t_concrete_wall );
                    } else {
                        ter_set( point( i, j ), t_floor );
                    }
                }
            }
            ter_set( point( SEEX - 1, 0 ), t_door_metal_locked );
            ter_set( point( SEEX - 1, 1 ), t_floor );
            ter_set( point( SEEX, 0 ), t_door_metal_locked );
            ter_set( point( SEEX, 1 ), t_floor );
            ter_set( point( SEEX - 2 + rng( 0, 1 ) * 3, 0 ), t_card_science );
            ter_set( point( SEEX - 2, SEEY ), t_door_metal_c );
            ter_set( point( SEEX + 1, SEEY ), t_door_metal_c );
            ter_set( point( SEEX - 2, SEEY - 1 ), t_door_metal_c );
            ter_set( point( SEEX + 1, SEEY - 1 ), t_door_metal_c );
            ter_set( point( SEEX - 1, SEEY * 2 - 3 ), t_stairs_down );
            ter_set( point( SEEX, SEEY * 2 - 3 ), t_stairs_down );
            science_room( this, point( 2, 2 ), point( SEEX - 3, SEEY * 2 - 3 ), dat.zlevel(), 1 );
            science_room( this, point( SEEX + 2, 2 ), point( SEEX * 2 - 3, SEEY * 2 - 3 ), dat.zlevel(), 3 );

            place_spawns( GROUP_TURRET, 1, point( SEEX, 5 ), point( SEEX, 5 ), 1, true );

            if( is_ot_match( "road", dat.east(), ot_match_type::type ) ) {
                rotate( 1 );
            } else if( is_ot_match( "road", dat.south(), ot_match_type::type ) ) {
                rotate( 2 );
            } else if( is_ot_match( "road", dat.west(), ot_match_type::type ) ) {
                rotate( 3 );
            }
        } else if( tw != 0 || rw != 0 || lw != 0 || bw != 0 ) { // Sewers!
            for( int i = 0; i < SEEX * 2; i++ ) {
                for( int j = 0; j < SEEY * 2; j++ ) {
                    ter_set( point( i, j ), t_thconc_floor );
                    if( ( ( i < lw || i > EAST_EDGE - rw ) && j > SEEY - 3 && j < SEEY + 2 ) ||
                        ( ( j < tw || j > SOUTH_EDGE - bw ) && i > SEEX - 3 && i < SEEX + 2 ) ) {
                        ter_set( point( i, j ), t_sewage );
                    }
                    if( ( i == 0 && is_ot_match( "lab", dat.east(), ot_match_type::contains ) ) || i == EAST_EDGE ) {
                        if( ter( point( i, j ) ) == t_sewage ) {
                            ter_set( point( i, j ), t_bars );
                        } else if( j == SEEY - 1 || j == SEEY ) {
                            ter_set( point( i, j ), t_door_metal_c );
                        } else {
                            ter_set( point( i, j ), t_concrete_wall );
                        }
                    } else if( ( j == 0 && is_ot_match( "lab", dat.north(), ot_match_type::contains ) ) ||
                               j == SOUTH_EDGE ) {
                        if( ter( point( i, j ) ) == t_sewage ) {
                            ter_set( point( i, j ), t_bars );
                        } else if( i == SEEX - 1 || i == SEEX ) {
                            ter_set( point( i, j ), t_door_metal_c );
                        } else {
                            ter_set( point( i, j ), t_concrete_wall );
                        }
                    }
                }
            }
        } else { // We're below ground, and no sewers
            // Set up the boundaries of walls (connect to adjacent lab squares)
            tw = is_ot_match( "lab", dat.north(), ot_match_type::contains ) ? 0 : 2;
            rw = is_ot_match( "lab", dat.east(), ot_match_type::contains ) ? 1 : 2;
            bw = is_ot_match( "lab", dat.south(), ot_match_type::contains ) ? 1 : 2;
            lw = is_ot_match( "lab", dat.west(), ot_match_type::contains ) ? 0 : 2;

            int boarders = 0;
            if( tw == 0 ) {
                boarders++;
            }
            if( rw == 1 ) {
                boarders++;
            }
            if( bw == 1 ) {
                boarders++;
            }
            if( lw == 0 ) {
                boarders++;
            }

            const auto maybe_insert_stairs = [this]( const oter_id & terrain,  const ter_id & t_stair_type ) {
                if( is_ot_match( "stairs", terrain, ot_match_type::contains ) ) {
                    const auto predicate = [this]( const tripoint & p ) {
                        return ter( p ) == t_thconc_floor && furn( p ) == f_null && tr_at( p ).is_null();
                    };
                    const auto range = points_in_rectangle( { 0, 0, abs_sub.z }, { SEEX * 2 - 2, SEEY * 2 - 2, abs_sub.z } );

                    if( const auto p = random_point( range, predicate ) ) {
                        ter_set( *p, t_stair_type );
                    }
                }
            };

            //A lab area with only one entrance
            if( boarders == 1 ) {
                // If you remove the usage of "lab_1side" here, remove it from mapgen_factory::get_usages above as well.
                if( oter_mapgen.generate( dat, "lab_1side" ) ) {
                    if( tw == 2 ) {
                        rotate( 2 );
                    }
                    if( rw == 2 ) {
                        rotate( 1 );
                    }
                    if( lw == 2 ) {
                        rotate( 3 );
                    }
                } else {
                    debugmsg( "Error: Tried to generate 1-sided lab but no lab_1side json exists." );
                }
                maybe_insert_stairs( dat.above(), t_stairs_up );
                maybe_insert_stairs( terrain_type, t_stairs_down );
            } else {
                const int hardcoded_4side_map_weight = 1500; // weight of all hardcoded maps.
                // If you remove the usage of "lab_4side" here, remove it from mapgen_factory::get_usages above as well.
                if( oter_mapgen.generate( dat, "lab_4side", hardcoded_4side_map_weight ) ) {
                    // If the map template hasn't handled borders, handle them in code.
                    // Rotated maps cannot handle borders and have to be caught in code.
                    // We determine if a border isn't handled by checking the east-facing
                    // border space where the door normally is -- it should be a wall or door.
                    tripoint east_border( 23, 11, abs_sub.z );
                    if( !has_flag_ter( "WALL", east_border ) &&
                        !has_flag_ter( "DOOR", east_border ) ) {
                        // TODO: create a ter_reset function that does ter_set,
                        // furn_set, and i_clear?
                        ter_id lw_type = tower_lab ? t_reinforced_glass : t_concrete_wall;
                        ter_id tw_type = tower_lab ? t_reinforced_glass : t_concrete_wall;
                        ter_id rw_type = tower_lab && rw == 2 ? t_reinforced_glass :
                                         t_concrete_wall;
                        ter_id bw_type = tower_lab && bw == 2 ? t_reinforced_glass :
                                         t_concrete_wall;
                        for( int i = 0; i < SEEX * 2; i++ ) {
                            ter_set( point( 23, i ), rw_type );
                            furn_set( point( 23, i ), f_null );
                            i_clear( tripoint( 23, i, get_abs_sub().z ) );

                            ter_set( point( i, 23 ), bw_type );
                            furn_set( point( i, 23 ), f_null );
                            i_clear( tripoint( i, 23, get_abs_sub().z ) );

                            if( lw == 2 ) {
                                ter_set( point( 0, i ), lw_type );
                                furn_set( point( 0, i ), f_null );
                                i_clear( tripoint( 0, i, get_abs_sub().z ) );
                            }
                            if( tw == 2 ) {
                                ter_set( point( i, 0 ), tw_type );
                                furn_set( point( i, 0 ), f_null );
                                i_clear( tripoint( i, 0, get_abs_sub().z ) );
                            }
                        }
                        if( rw != 2 ) {
                            ter_set( point( 23, 11 ), t_door_metal_c );
                            ter_set( point( 23, 12 ), t_door_metal_c );
                        }
                        if( bw != 2 ) {
                            ter_set( point( 11, 23 ), t_door_metal_c );
                            ter_set( point( 12, 23 ), t_door_metal_c );
                        }
                    }

                    maybe_insert_stairs( dat.above(), t_stairs_up );
                    maybe_insert_stairs( terrain_type, t_stairs_down );
                } else { // then no json maps for lab_4side were found
                    switch( rng( 1, 3 ) ) {
                        case 1:
                            // Cross shaped
                            for( int i = 0; i < SEEX * 2; i++ ) {
                                for( int j = 0; j < SEEY * 2; j++ ) {
                                    if( ( i < lw || i > EAST_EDGE - rw ) ||
                                        ( ( j < SEEY - 1 || j > SEEY ) &&
                                          ( i == SEEX - 2 || i == SEEX + 1 ) ) ) {
                                        ter_set( point( i, j ), t_concrete_wall );
                                    } else if( ( j < tw || j > SOUTH_EDGE - bw ) ||
                                               ( ( i < SEEX - 1 || i > SEEX ) &&
                                                 ( j == SEEY - 2 || j == SEEY + 1 ) ) ) {
                                        ter_set( point( i, j ), t_concrete_wall );
                                    } else {
                                        ter_set( point( i, j ), t_thconc_floor );
                                    }
                                }
                            }
                            if( is_ot_match( "stairs", dat.above(), ot_match_type::contains ) ) {
                                ter_set( point( rng( SEEX - 1, SEEX ), rng( SEEY - 1, SEEY ) ),
                                         t_stairs_up );
                            }
                            // Top left
                            if( one_in( 2 ) ) {
                                ter_set( point( SEEX - 2, int( SEEY / 2 ) ), t_door_glass_frosted_c );
                                science_room( this, point( lw, tw ), point( SEEX - 3, SEEY - 3 ), dat.zlevel(), 1 );
                            } else {
                                ter_set( point( SEEX / 2, SEEY - 2 ), t_door_glass_frosted_c );
                                science_room( this, point( lw, tw ), point( SEEX - 3, SEEY - 3 ), dat.zlevel(), 2 );
                            }
                            // Top right
                            if( one_in( 2 ) ) {
                                ter_set( point( SEEX + 1, int( SEEY / 2 ) ), t_door_glass_frosted_c );
                                science_room( this, point( SEEX + 2, tw ), point( EAST_EDGE - rw, SEEY - 3 ),
                                              dat.zlevel(), 3 );
                            } else {
                                ter_set( point( SEEX + int( SEEX / 2 ), SEEY - 2 ), t_door_glass_frosted_c );
                                science_room( this, point( SEEX + 2, tw ), point( EAST_EDGE - rw, SEEY - 3 ),
                                              dat.zlevel(), 2 );
                            }
                            // Bottom left
                            if( one_in( 2 ) ) {
                                ter_set( point( SEEX / 2, SEEY + 1 ), t_door_glass_frosted_c );
                                science_room( this, point( lw, SEEY + 2 ), point( SEEX - 3, SOUTH_EDGE - bw ),
                                              dat.zlevel(), 0 );
                            } else {
                                ter_set( point( SEEX - 2, SEEY + int( SEEY / 2 ) ), t_door_glass_frosted_c );
                                science_room( this, point( lw, SEEY + 2 ), point( SEEX - 3, SOUTH_EDGE - bw ),
                                              dat.zlevel(), 1 );
                            }
                            // Bottom right
                            if( one_in( 2 ) ) {
                                ter_set( point( SEEX + int( SEEX / 2 ), SEEY + 1 ), t_door_glass_frosted_c );
                                science_room( this, point( SEEX + 2, SEEY + 2 ), point( EAST_EDGE - rw, SOUTH_EDGE - bw ),
                                              dat.zlevel(), 0 );
                            } else {
                                ter_set( point( SEEX + 1, SEEY + int( SEEY / 2 ) ), t_door_glass_frosted_c );
                                science_room( this, point( SEEX + 2, SEEY + 2 ), point( EAST_EDGE - rw, SOUTH_EDGE - bw ),
                                              dat.zlevel(), 3 );
                            }
                            if( rw == 1 ) {
                                ter_set( point( EAST_EDGE, SEEY - 1 ), t_door_metal_c );
                                ter_set( point( EAST_EDGE, SEEY ), t_door_metal_c );
                            }
                            if( bw == 1 ) {
                                ter_set( point( SEEX - 1, SOUTH_EDGE ), t_door_metal_c );
                                ter_set( point( SEEX, SOUTH_EDGE ), t_door_metal_c );
                            }
                            if( is_ot_match( "stairs", terrain_type, ot_match_type::contains ) ) { // Stairs going down
                                std::vector<point> stair_points;
                                if( tw != 0 ) {
                                    stair_points.emplace_back( SEEX - 1, 2 );
                                    stair_points.emplace_back( SEEX - 1, 2 );
                                    stair_points.emplace_back( SEEX, 2 );
                                    stair_points.emplace_back( SEEX, 2 );
                                }
                                if( rw != 1 ) {
                                    stair_points.emplace_back( SEEX * 2 - 3, SEEY - 1 );
                                    stair_points.emplace_back( SEEX * 2 - 3, SEEY - 1 );
                                    stair_points.emplace_back( SEEX * 2 - 3, SEEY );
                                    stair_points.emplace_back( SEEX * 2 - 3, SEEY );
                                }
                                if( bw != 1 ) {
                                    stair_points.emplace_back( SEEX - 1, SEEY * 2 - 3 );
                                    stair_points.emplace_back( SEEX - 1, SEEY * 2 - 3 );
                                    stair_points.emplace_back( SEEX, SEEY * 2 - 3 );
                                    stair_points.emplace_back( SEEX, SEEY * 2 - 3 );
                                }
                                if( lw != 0 ) {
                                    stair_points.emplace_back( 2, SEEY - 1 );
                                    stair_points.emplace_back( 2, SEEY - 1 );
                                    stair_points.emplace_back( 2, SEEY );
                                    stair_points.emplace_back( 2, SEEY );
                                }
                                stair_points.emplace_back( int( SEEX / 2 ), SEEY );
                                stair_points.emplace_back( int( SEEX / 2 ), SEEY - 1 );
                                stair_points.emplace_back( int( SEEX / 2 ) + SEEX, SEEY );
                                stair_points.emplace_back( int( SEEX / 2 ) + SEEX, SEEY - 1 );
                                stair_points.emplace_back( SEEX, int( SEEY / 2 ) );
                                stair_points.emplace_back( SEEX + 2, int( SEEY / 2 ) );
                                stair_points.emplace_back( SEEX, int( SEEY / 2 ) + SEEY );
                                stair_points.emplace_back( SEEX + 2, int( SEEY / 2 ) + SEEY );
                                const point p = random_entry( stair_points );
                                ter_set( p, t_stairs_down );
                            }

                            break;

                        case 2:
                            // tic-tac-toe # layout
                            for( int i = 0; i < SEEX * 2; i++ ) {
                                for( int j = 0; j < SEEY * 2; j++ ) {
                                    if( i < lw || i > EAST_EDGE - rw || i == SEEX - 4 ||
                                        i == SEEX + 3 ) {
                                        ter_set( point( i, j ), t_concrete_wall );
                                    } else if( j < tw || j > SOUTH_EDGE - bw || j == SEEY - 4 ||
                                               j == SEEY + 3 ) {
                                        ter_set( point( i, j ), t_concrete_wall );
                                    } else {
                                        ter_set( point( i, j ), t_thconc_floor );
                                    }
                                }
                            }
                            if( is_ot_match( "stairs", dat.above(), ot_match_type::contains ) ) {
                                ter_set( point( SEEX - 1, SEEY - 1 ), t_stairs_up );
                                ter_set( point( SEEX, SEEY - 1 ), t_stairs_up );
                                ter_set( point( SEEX - 1, SEEY ), t_stairs_up );
                                ter_set( point( SEEX, SEEY ), t_stairs_up );
                            }
                            ter_set( point( SEEX - rng( 0, 1 ), SEEY - 4 ), t_door_glass_frosted_c );
                            ter_set( point( SEEX - rng( 0, 1 ), SEEY + 3 ), t_door_glass_frosted_c );
                            ter_set( point( SEEX - 4, SEEY + rng( 0, 1 ) ), t_door_glass_frosted_c );
                            ter_set( point( SEEX + 3, SEEY + rng( 0, 1 ) ), t_door_glass_frosted_c );
                            ter_set( point( SEEX - 4, int( SEEY / 2 ) ), t_door_glass_frosted_c );
                            ter_set( point( SEEX + 3, int( SEEY / 2 ) ), t_door_glass_frosted_c );
                            ter_set( point( SEEX / 2, SEEY - 4 ), t_door_glass_frosted_c );
                            ter_set( point( SEEX / 2, SEEY + 3 ), t_door_glass_frosted_c );
                            ter_set( point( SEEX + int( SEEX / 2 ), SEEY - 4 ), t_door_glass_frosted_c );
                            ter_set( point( SEEX + int( SEEX / 2 ), SEEY + 3 ), t_door_glass_frosted_c );
                            ter_set( point( SEEX - 4, SEEY + int( SEEY / 2 ) ), t_door_glass_frosted_c );
                            ter_set( point( SEEX + 3, SEEY + int( SEEY / 2 ) ), t_door_glass_frosted_c );
                            science_room( this, point( lw, tw ), point( SEEX - 5, SEEY - 5 ), dat.zlevel(),
                                          rng( 1, 2 ) );
                            science_room( this, point( SEEX - 3, tw ), point( SEEX + 2, SEEY - 5 ), dat.zlevel(), 2 );
                            science_room( this, point( SEEX + 4, tw ), point( EAST_EDGE - rw, SEEY - 5 ),
                                          dat.zlevel(), rng( 2, 3 ) );
                            science_room( this, point( lw, SEEY - 3 ), point( SEEX - 5, SEEY + 2 ), dat.zlevel(), 1 );
                            science_room( this, point( SEEX + 4, SEEY - 3 ), point( EAST_EDGE - rw, SEEY + 2 ),
                                          dat.zlevel(), 3 );
                            science_room( this, point( lw, SEEY + 4 ), point( SEEX - 5, SOUTH_EDGE - bw ),
                                          dat.zlevel(), rng( 0, 1 ) );
                            science_room( this, point( SEEX - 3, SEEY + 4 ), point( SEEX + 2, SOUTH_EDGE - bw ),
                                          dat.zlevel(), 0 );
                            science_room( this, point( SEEX + 4, SEEX + 4 ), point( EAST_EDGE - rw, SOUTH_EDGE - bw ),
                                          dat.zlevel(), 3 * rng( 0, 1 ) );
                            if( rw == 1 ) {
                                ter_set( point( EAST_EDGE, SEEY - 1 ), t_door_metal_c );
                                ter_set( point( EAST_EDGE, SEEY ), t_door_metal_c );
                            }
                            if( bw == 1 ) {
                                ter_set( point( SEEX - 1, SOUTH_EDGE ), t_door_metal_c );
                                ter_set( point( SEEX, SOUTH_EDGE ), t_door_metal_c );
                            }
                            if( is_ot_match( "stairs", terrain_type, ot_match_type::contains ) ) {
                                ter_set( point( SEEX - 3 + 5 * rng( 0, 1 ), SEEY - 3 + 5 * rng( 0, 1 ) ),
                                         t_stairs_down );
                            }
                            break;

                        case 3:
                            // Big room
                            for( int i = 0; i < SEEX * 2; i++ ) {
                                for( int j = 0; j < SEEY * 2; j++ ) {
                                    if( i < lw || i >= EAST_EDGE - rw ) {
                                        ter_set( point( i, j ), t_concrete_wall );
                                    } else if( j < tw || j >= SOUTH_EDGE - bw ) {
                                        ter_set( point( i, j ), t_concrete_wall );
                                    } else {
                                        ter_set( point( i, j ), t_thconc_floor );
                                    }
                                }
                            }
                            science_room( this, point( lw, tw ), point( EAST_EDGE - rw, SOUTH_EDGE - bw ),
                                          dat.zlevel(), rng( 0, 3 ) );

                            if( rw == 1 ) {
                                ter_set( point( EAST_EDGE, SEEY - 1 ), t_door_metal_c );
                                ter_set( point( EAST_EDGE, SEEY ), t_door_metal_c );
                            }
                            if( bw == 1 ) {
                                ter_set( point( SEEX - 1, SOUTH_EDGE ), t_door_metal_c );
                                ter_set( point( SEEX, SOUTH_EDGE ), t_door_metal_c );
                            }
                            maybe_insert_stairs( dat.above(), t_stairs_up );
                            maybe_insert_stairs( terrain_type, t_stairs_down );
                            break;
                    }
                } // endif use_hardcoded_4side_map
            }  // end 1 vs 4 sides
        } // end aboveground vs belowground

        // Ants will totally wreck up the place
        if( ants_lab ) {
            for( int i = 0; i < SEEX * 2; i++ ) {
                for( int j = 0; j < SEEY * 2; j++ ) {
                    // Carve out a diamond area that covers 2 spaces on each edge.
                    if( i + j > 10 && i + j < 36 && std::abs( i - j ) < 13 ) {
                        // Doors and walls get sometimes destroyed:
                        // 100% at the edge, usually in a central cross, occasionally elsewhere.
                        if( ( has_flag_ter( "DOOR", point( i, j ) ) || has_flag_ter( "WALL", point( i, j ) ) ) ) {
                            if( ( i == 0 || j == 0 || i == 23 || j == 23 ) ||
                                ( !one_in( 3 ) && ( i == 11 || i == 12 || j == 11 || j == 12 ) ) ||
                                one_in( 4 ) ) {
                                // bash and usually remove the rubble.
                                make_rubble( { i, j, abs_sub.z } );
                                ter_set( point( i, j ), t_rock_floor );
                                if( !one_in( 3 ) ) {
                                    furn_set( point( i, j ), f_null );
                                }
                            }
                            // and then randomly destroy 5% of the remaining nonstairs.
                        } else if( one_in( 20 ) &&
                                   !has_flag_ter( "GOES_DOWN", p2 ) &&
                                   !has_flag_ter( "GOES_UP", p2 ) ) {
                            destroy( { i, j, abs_sub.z } );
                            // bashed squares can create dirt & floors, but we want rock floors.
                            if( t_dirt == ter( point( i, j ) ) || t_floor == ter( point( i, j ) ) ) {
                                ter_set( point( i, j ), t_rock_floor );
                            }
                        }
                    }
                }
            }
        }

        // Slimes pretty much wreck up the place, too, but only underground
        tw = ( dat.north() == "slimepit" ? SEEY     : 0 );
        rw = ( dat.east()  == "slimepit" ? SEEX + 1 : 0 );
        bw = ( dat.south() == "slimepit" ? SEEY + 1 : 0 );
        lw = ( dat.west()  == "slimepit" ? SEEX     : 0 );
        if( tw != 0 || rw != 0 || bw != 0 || lw != 0 ) {
            for( int i = 0; i < SEEX * 2; i++ ) {
                for( int j = 0; j < SEEY * 2; j++ ) {
                    if( ( ( j <= tw || i >= rw ) && i >= j && ( EAST_EDGE - i ) <= j ) ||
                        ( ( j >= bw || i <= lw ) && i <= j && ( SOUTH_EDGE - j ) <= i ) ) {
                        if( one_in( 5 ) ) {
                            make_rubble( tripoint( i,  j, abs_sub.z ), f_rubble_rock,
                                         t_slime );
                        } else if( !one_in( 5 ) ) {
                            ter_set( point( i, j ), t_slime );
                        }
                    }
                }
            }
        }

        int light_odds = 0;
        // central labs are always fully lit, other labs have half chance of some lights.
        if( central_lab ) {
            light_odds = 1;
        } else if( one_in( 2 ) ) {
            // Create a spread of densities, from all possible lights on, to 1/3, ...
            // to ~1 per segment.
            light_odds = std::pow( rng( 1, 12 ), 1.6 );
        }
        if( light_odds > 0 ) {
            for( int i = 0; i < SEEX * 2; i++ ) {
                for( int j = 0; j < SEEY * 2; j++ ) {
                    if( !( ( i * j ) % 2 || ( i + j ) % 4 ) && one_in( light_odds ) ) {
                        if( t_thconc_floor == ter( point( i, j ) ) || t_strconc_floor == ter( point( i, j ) ) ) {
                            ter_set( point( i, j ), t_thconc_floor_olight );
                        }
                    }
                }
            }
        }

        if( tower_lab ) {
            place_spawns( GROUP_LAB, 1, point_zero, point( EAST_EDGE, EAST_EDGE ), abs_sub.z * 0.02f );
        }

        // Lab special effects.
        if( one_in( 10 ) ) {
            switch( rng( 1, 7 ) ) {
                // full flooding/sewage
                case 1: {
                    if( is_ot_match( "stairs", terrain_type, ot_match_type::contains ) ||
                        is_ot_match( "ice", terrain_type, ot_match_type::contains ) ) {
                        // don't flood if stairs because the floor below will not be flooded.
                        // don't flood if ice lab because there's no mechanic for freezing
                        // liquid floors.
                        break;
                    }
                    auto fluid_type = one_in( 3 ) ? t_sewage : t_water_sh;
                    for( int i = 0; i < EAST_EDGE; i++ ) {
                        for( int j = 0; j < SOUTH_EDGE; j++ ) {
                            // We spare some terrain to make it look better visually.
                            if( !one_in( 10 ) && ( t_thconc_floor == ter( point( i, j ) ) ||
                                                   t_strconc_floor == ter( point( i, j ) ) ||
                                                   t_thconc_floor_olight == ter( point( i, j ) ) ) ) {
                                ter_set( point( i, j ), fluid_type );
                            } else if( has_flag_ter( "DOOR", point( i, j ) ) && !one_in( 3 ) ) {
                                // We want the actual debris, but not the rubble marker or dirt.
                                make_rubble( { i, j, abs_sub.z } );
                                ter_set( point( i, j ), fluid_type );
                                furn_set( point( i, j ), f_null );
                            }
                        }
                    }
                    break;
                }
                // minor flooding/sewage
                case 2: {
                    if( is_ot_match( "stairs", terrain_type, ot_match_type::contains ) ||
                        is_ot_match( "ice", terrain_type, ot_match_type::contains ) ) {
                        // don't flood if stairs because the floor below will not be flooded.
                        // don't flood if ice lab because there's no mechanic for freezing
                        // liquid floors.
                        break;
                    }
                    auto fluid_type = one_in( 3 ) ? t_sewage : t_water_sh;
                    for( int i = 0; i < 2; ++i ) {
                        draw_rough_circle( [this, fluid_type]( point  p ) {
                            if( t_thconc_floor == ter( p ) || t_strconc_floor == ter( p ) ||
                                t_thconc_floor_olight == ter( p ) ) {
                                ter_set( p, fluid_type );
                            } else if( has_flag_ter( "DOOR", p ) ) {
                                // We want the actual debris, but not the rubble marker or dirt.
                                make_rubble( { p, abs_sub.z } );
                                ter_set( p, fluid_type );
                                furn_set( p, f_null );
                            }
                        }, point( rng( 1, SEEX * 2 - 2 ), rng( 1, SEEY * 2 - 2 ) ), rng( 3, 6 ) );
                    }
                    break;
                }
                // toxic gas leaks and smoke-filled rooms.
                case 3:
                case 4: {
                    bool is_toxic = one_in( 3 );
                    for( int i = 0; i < SEEX * 2; i++ ) {
                        for( int j = 0; j < SEEY * 2; j++ ) {
                            if( one_in( 200 ) && ( t_thconc_floor == ter( point( i, j ) ) ||
                                                   t_strconc_floor == ter( point( i, j ) ) ) ) {
                                if( is_toxic ) {
                                    add_field( {i, j, abs_sub.z}, fd_gas_vent, 1 );
                                } else {
                                    add_field( {i, j, abs_sub.z}, fd_smoke_vent, 2 );
                                }
                            }
                        }
                    }
                    break;
                }
                // portal with an artifact effect.
                case 5: {
                    tripoint center( rng( 6, SEEX * 2 - 7 ), rng( 6, SEEY * 2 - 7 ), abs_sub.z );
                    std::vector<artifact_natural_property> valid_props = {
                        ARTPROP_BREATHING,
                        ARTPROP_CRACKLING,
                        ARTPROP_WARM,
                        ARTPROP_SCALED,
                        ARTPROP_WHISPERING,
                        ARTPROP_GLOWING
                    };
                    draw_rough_circle( [this]( point  p ) {
                        if( has_flag_ter( "GOES_DOWN", p ) ||
                            has_flag_ter( "GOES_UP", p ) ||
                            has_flag_ter( "CONSOLE", p ) ) {
                            return; // spare stairs and consoles.
                        }
                        make_rubble( {p, abs_sub.z } );
                        ter_set( p, t_thconc_floor );
                    }, center.xy(), 4 );
                    furn_set( center.xy(), f_null );
                    trap_set( center, tr_portal );
                    create_anomaly( center, random_entry( valid_props ), false );
                    break;
                }
                // radioactive accident.
                case 6: {
                    tripoint center( rng( 6, SEEX * 2 - 7 ), rng( 6, SEEY * 2 - 7 ), abs_sub.z );
                    if( has_flag_ter( "WALL", center.xy() ) ) {
                        // just skip it, we don't want to risk embedding radiation out of sight.
                        break;
                    }
                    draw_rough_circle( [this]( point  p ) {
                        set_radiation( p, 10 );
                    }, center.xy(), rng( 7, 12 ) );
                    draw_circle( [this]( point  p ) {
                        set_radiation( p, 20 );
                    }, center.xy(), rng( 5, 8 ) );
                    draw_circle( [this]( point  p ) {
                        set_radiation( p, 30 );
                    }, center.xy(), rng( 2, 4 ) );
                    draw_circle( [this]( point  p ) {
                        set_radiation( p, 50 );
                    }, center.xy(), 1 );
                    draw_circle( [this]( point  p ) {
                        if( has_flag_ter( "GOES_DOWN", p ) ||
                            has_flag_ter( "GOES_UP", p ) ||
                            has_flag_ter( "CONSOLE", p ) ) {
                            return; // spare stairs and consoles.
                        }
                        make_rubble( {p, abs_sub.z } );
                        ter_set( p, t_thconc_floor );
                    }, center.xy(), 1 );

                    place_spawns( GROUP_HAZMATBOT, 1, center.xy() + point_west,
                                  center.xy() + point_west, 1, true );
                    place_spawns( GROUP_HAZMATBOT, 2, center.xy() + point_west,
                                  center.xy() + point_west, 1, true );

                    // damaged mininuke/plut thrown past edge of rubble so the player can see it.
                    int marker_x = center.x - 2 + 4 * rng( 0, 1 );
                    int marker_y = center.y + rng( -2, 2 );
                    if( one_in( 4 ) ) {
                        spawn_item(
                            point( marker_x, marker_y ), "mininuke", 1, 1, calendar::start_of_cataclysm, rng( 2, 4 )
                        );
                    } else {
                        detached_ptr<item> newliquid = item::spawn( "plut_slurry_dense", calendar::start_of_cataclysm );
                        newliquid->charges = 1;
                        add_item_or_charges( tripoint( marker_x, marker_y, get_abs_sub().z ),
                                             std::move( newliquid ) );
                    }
                    break;
                }
                // portal with fungal invasion
                case 7: {
                    for( int i = 0; i < EAST_EDGE; i++ ) {
                        for( int j = 0; j < SOUTH_EDGE; j++ ) {
                            // Create a mostly spread fungal area throughout entire lab.
                            if( !one_in( 5 ) && ( has_flag( "FLAT", point( i, j ) ) ) ) {
                                ter_set( point( i, j ), t_fungus_floor_in );
                                if( has_flag_furn( "ORGANIC", point( i, j ) ) ) {
                                    furn_set( point( i, j ), f_fungal_clump );
                                }
                            } else if( has_flag_ter( "DOOR", point( i, j ) ) && !one_in( 5 ) ) {
                                ter_set( point( i, j ), t_fungus_floor_in );
                            } else if( has_flag_ter( "WALL", point( i, j ) ) && one_in( 3 ) ) {
                                ter_set( point( i, j ), t_fungus_wall );
                            }
                        }
                    }
                    tripoint center( rng( 6, SEEX * 2 - 7 ), rng( 6, SEEY * 2 - 7 ), abs_sub.z );

                    // Make a portal surrounded by more dense fungal stuff and a fungaloid.
                    draw_rough_circle( [this]( point  p ) {
                        if( has_flag_ter( "GOES_DOWN", p ) ||
                            has_flag_ter( "GOES_UP", p ) ||
                            has_flag_ter( "CONSOLE", p ) ) {
                            return; // spare stairs and consoles.
                        }
                        if( has_flag_ter( "WALL", p ) ) {
                            ter_set( p, t_fungus_wall );
                        } else {
                            ter_set( p, t_fungus_floor_in );
                            if( one_in( 3 ) ) {
                                furn_set( p, f_flower_fungal );
                            } else if( one_in( 10 ) ) {
                                ter_set( p, t_marloss );
                            }
                        }
                    }, center.xy(), 3 );
                    ter_set( center.xy(), t_fungus_floor_in );
                    furn_set( center.xy(), f_null );
                    trap_set( center, tr_portal );
                    place_spawns( GROUP_FUNGI_FUNGALOID, 1, center.xy() + point( -2, -2 ),
                                  center.xy() + point( 2, 2 ), 1, true );

                    break;
                }
            }
        }
    } else if( terrain_type == "lab_finale" || terrain_type == "ice_lab_finale" ||
               terrain_type == "central_lab_finale" || terrain_type == "tower_lab_finale" ) {

        ice_lab = is_ot_match( "ice_lab", terrain_type, ot_match_type::prefix );
        central_lab = is_ot_match( "central_lab", terrain_type, ot_match_type::prefix );
        tower_lab = is_ot_match( "tower_lab", terrain_type, ot_match_type::prefix );

        if( ice_lab ) {
            int temperature = -20 + 30 * dat.zlevel();
            set_temperature( p2, temperature );
            set_temperature( p2 + point( SEEX, 0 ), temperature );
            set_temperature( p2 + point( 0, SEEY ), temperature );
            set_temperature( p2 + point( SEEX, SEEY ), temperature );
        }

        tw = is_ot_match( "lab", dat.north(), ot_match_type::contains ) ? 0 : 2;
        rw = is_ot_match( "lab", dat.east(), ot_match_type::contains ) ? 1 : 2;
        bw = is_ot_match( "lab", dat.south(), ot_match_type::contains ) ? 1 : 2;
        lw = is_ot_match( "lab", dat.west(), ot_match_type::contains ) ? 0 : 2;

        // If you remove the usage of "lab_finale_1level" here, remove it from mapgen_factory::get_usages above as well.
        if( oter_mapgen.generate( dat, "lab_finale_1level" ) ) {
            // If the map template hasn't handled borders, handle them in code.
            // Rotated maps cannot handle borders and have to be caught in code.
            // We determine if a border isn't handled by checking the east-facing
            // border space where the door normally is -- it should be a wall or door.
            tripoint east_border( 23, 11, abs_sub.z );
            if( !has_flag_ter( "WALL", east_border ) && !has_flag_ter( "DOOR", east_border ) ) {
                // TODO: create a ter_reset function that does ter_set, furn_set, and i_clear?
                ter_id lw_type = tower_lab ? t_reinforced_glass : t_concrete_wall;
                ter_id tw_type = tower_lab ? t_reinforced_glass : t_concrete_wall;
                ter_id rw_type = tower_lab && rw == 2 ? t_reinforced_glass : t_concrete_wall;
                ter_id bw_type = tower_lab && bw == 2 ? t_reinforced_glass : t_concrete_wall;
                for( int i = 0; i < SEEX * 2; i++ ) {
                    ter_set( point( 23, i ), rw_type );
                    furn_set( point( 23, i ), f_null );
                    i_clear( tripoint( 23, i, get_abs_sub().z ) );

                    ter_set( point( i, 23 ), bw_type );
                    furn_set( point( i, 23 ), f_null );
                    i_clear( tripoint( i, 23, get_abs_sub().z ) );

                    if( lw == 2 ) {
                        ter_set( point( 0, i ), lw_type );
                        furn_set( point( 0, i ), f_null );
                        i_clear( tripoint( 0, i, get_abs_sub().z ) );
                    }
                    if( tw == 2 ) {
                        ter_set( point( i, 0 ), tw_type );
                        furn_set( point( i, 0 ), f_null );
                        i_clear( tripoint( i, 0, get_abs_sub().z ) );
                    }
                }
                if( rw != 2 ) {
                    ter_set( point( 23, 11 ), t_door_metal_c );
                    ter_set( point( 23, 12 ), t_door_metal_c );
                }
                if( bw != 2 ) {
                    ter_set( point( 11, 23 ), t_door_metal_c );
                    ter_set( point( 12, 23 ), t_door_metal_c );
                }
            }
        }

        // Handle stairs in the unlikely case they are needed.
        const auto maybe_insert_stairs = [this]( const oter_id & terrain,  const ter_id & t_stair_type ) {
            if( is_ot_match( "stairs", terrain, ot_match_type::contains ) ) {
                const auto predicate = [this]( const tripoint & p ) {
                    return ter( p ) == t_thconc_floor && furn( p ) == f_null &&
                           tr_at( p ).is_null();
                };
                const auto range = points_in_rectangle( { 0, 0, abs_sub.z },
                { SEEX * 2 - 2, SEEY * 2 - 2, abs_sub.z } );
                if( const auto p = random_point( range, predicate ) ) {
                    ter_set( *p, t_stair_type );
                }
            }
        };
        maybe_insert_stairs( dat.above(), t_stairs_up );
        maybe_insert_stairs( terrain_type, t_stairs_down );

        int light_odds = 0;
        // central labs are always fully lit, other labs have half chance of some lights.
        if( central_lab ) {
            light_odds = 1;
        } else if( one_in( 2 ) ) {
            light_odds = std::pow( rng( 1, 12 ), 1.6 );
        }
        if( light_odds > 0 ) {
            for( int i = 0; i < SEEX * 2; i++ ) {
                for( int j = 0; j < SEEY * 2; j++ ) {
                    if( !( ( i * j ) % 2 || ( i + j ) % 4 ) && one_in( light_odds ) ) {
                        if( t_thconc_floor == ter( point( i, j ) ) || t_strconc_floor == ter( point( i, j ) ) ) {
                            ter_set( point( i, j ), t_thconc_floor_olight );
                        }
                    }
                }
            }
        }
    }
}

void map::draw_temple( const mapgendata &dat )
{
    const oter_id &terrain_type = dat.terrain_type();
    if( terrain_type == "temple" || terrain_type == "temple_stairs" ) {
        if( dat.zlevel() == 0 ) {
            // Ground floor
            // TODO: More varieties?
            fill_background( this, t_dirt );
            square( this, t_grate, point( SEEX - 1, SEEY - 1 ), point( SEEX, SEEX ) );
            ter_set( point( SEEX + 1, SEEY + 1 ), t_pedestal_temple );
        } else {
            // Underground!  Shit's about to get interesting!
            // Start with all rock floor
            square( this, t_rock_floor, point_zero, point( EAST_EDGE, SOUTH_EDGE ) );
            // We always start at the south and go north.
            // We use (y / 2 + z) % 4 to guarantee that rooms don't repeat.
            switch( 1 + std::abs( abs_sub.y / 2 + dat.zlevel() + 4 ) % 4 ) { // TODO: More varieties!

                case 1:
                    // Flame bursts
                    square( this, t_rock, point_zero, point( SEEX - 1, SOUTH_EDGE ) );
                    square( this, t_rock, point( SEEX + 2, 0 ), point( EAST_EDGE, SOUTH_EDGE ) );
                    for( int i = 2; i < SEEY * 2 - 4; i++ ) {
                        add_field( {SEEX, i, abs_sub.z}, fd_fire_vent, rng( 1, 3 ) );
                        add_field( {SEEX + 1, i, abs_sub.z}, fd_fire_vent, rng( 1, 3 ) );
                    }
                    break;

                case 2:
                    // Spreading water
                    square( this, t_water_dp, point( 4, 4 ), point( 5, 5 ) );
                    // replaced mon_sewer_snake spawn with GROUP_SEWER
                    // Decide whether a group of only sewer snakes be made, probably not worth it
                    place_spawns( GROUP_SEWER, 1, point( 4, 4 ), point( 4, 4 ), 1, true );

                    square( this, t_water_dp, point( SEEX * 2 - 5, 4 ), point( SEEX * 2 - 4, 6 ) );
                    place_spawns( GROUP_SEWER, 1, point( 1, SEEX * 2 - 5 ), point( 1, SEEX * 2 - 5 ), 1, true );

                    square( this, t_water_dp, point( 4, SEEY * 2 - 5 ), point( 6, SEEY * 2 - 4 ) );

                    square( this, t_water_dp, point( SEEX * 2 - 5, SEEY * 2 - 5 ), point( SEEX * 2 - 4,
                            SEEY * 2 - 4 ) );

                    square( this, t_rock, point( 0, SEEY * 2 - 2 ), point( SEEX - 1, SOUTH_EDGE ) );
                    square( this, t_rock, point( SEEX + 2, SEEY * 2 - 2 ), point( EAST_EDGE, SOUTH_EDGE ) );
                    line( this, t_grate, point( SEEX, 1 ), point( SEEX + 1, 1 ) ); // To drain the water
                    mtrap_set( this, point( SEEX, SEEY * 2 - 2 ), tr_temple_flood );
                    mtrap_set( this, point( SEEX + 1, SEEY * 2 - 2 ), tr_temple_flood );
                    for( int y = 2; y < SEEY * 2 - 2; y++ ) {
                        for( int x = 2; x < SEEX * 2 - 2; x++ ) {
                            if( ter( point( x, y ) ) == t_rock_floor && one_in( 4 ) ) {
                                mtrap_set( this, point( x, y ), tr_temple_flood );
                            }
                        }
                    }
                    break;

                case 3: { // Flipping walls puzzle
                    line( this, t_rock, point_zero, point( SEEX - 1, 0 ) );
                    line( this, t_rock, point( SEEX + 2, 0 ), point( EAST_EDGE, 0 ) );
                    line( this, t_rock, point( SEEX - 1, 1 ), point( SEEX - 1, 6 ) );
                    line( this, t_bars, point( SEEX + 2, 1 ), point( SEEX + 2, 6 ) );
                    ter_set( point( 14, 1 ), t_switch_rg );
                    ter_set( point( 15, 1 ), t_switch_gb );
                    ter_set( point( 16, 1 ), t_switch_rb );
                    ter_set( point( 17, 1 ), t_switch_even );
                    // Start with clear floors--then work backwards to the starting state
                    line( this, t_floor_red,   point( SEEX, 1 ), point( SEEX + 1, 1 ) );
                    line( this, t_floor_green, point( SEEX, 2 ), point( SEEX + 1, 2 ) );
                    line( this, t_floor_blue,  point( SEEX, 3 ), point( SEEX + 1, 3 ) );
                    line( this, t_floor_red,   point( SEEX, 4 ), point( SEEX + 1, 4 ) );
                    line( this, t_floor_green, point( SEEX, 5 ), point( SEEX + 1, 5 ) );
                    line( this, t_floor_blue,  point( SEEX, 6 ), point( SEEX + 1, 6 ) );
                    // Now, randomly choose actions
                    // Set up an actions vector so that there's not undue repetition
                    std::vector<int> actions;
                    actions.push_back( 1 );
                    actions.push_back( 2 );
                    actions.push_back( 3 );
                    actions.push_back( 4 );
                    actions.push_back( rng( 1, 3 ) );
                    while( !actions.empty() ) {
                        const int action = random_entry_removed( actions );
                        for( int y = 1; y < 7; y++ ) {
                            for( int x = SEEX; x <= SEEX + 1; x++ ) {
                                switch( action ) {
                                    case 1:
                                        // Toggle RG
                                        if( ter( point( x, y ) ) == t_floor_red ) {
                                            ter_set( point( x, y ), t_rock_red );
                                        } else if( ter( point( x, y ) ) == t_rock_red ) {
                                            ter_set( point( x, y ), t_floor_red );
                                        } else if( ter( point( x, y ) ) == t_floor_green ) {
                                            ter_set( point( x, y ), t_rock_green );
                                        } else if( ter( point( x, y ) ) == t_rock_green ) {
                                            ter_set( point( x, y ), t_floor_green );
                                        }
                                        break;
                                    case 2:
                                        // Toggle GB
                                        if( ter( point( x, y ) ) == t_floor_blue ) {
                                            ter_set( point( x, y ), t_rock_blue );
                                        } else if( ter( point( x, y ) ) == t_rock_blue ) {
                                            ter_set( point( x, y ), t_floor_blue );
                                        } else if( ter( point( x, y ) ) == t_floor_green ) {
                                            ter_set( point( x, y ), t_rock_green );
                                        } else if( ter( point( x, y ) ) == t_rock_green ) {
                                            ter_set( point( x, y ), t_floor_green );
                                        }
                                        break;
                                    case 3:
                                        // Toggle RB
                                        if( ter( point( x, y ) ) == t_floor_blue ) {
                                            ter_set( point( x, y ), t_rock_blue );
                                        } else if( ter( point( x, y ) ) == t_rock_blue ) {
                                            ter_set( point( x, y ), t_floor_blue );
                                        } else if( ter( point( x, y ) ) == t_floor_red ) {
                                            ter_set( point( x, y ), t_rock_red );
                                        } else if( ter( point( x, y ) ) == t_rock_red ) {
                                            ter_set( point( x, y ), t_floor_red );
                                        }
                                        break;
                                    case 4:
                                        // Toggle Even
                                        if( y % 2 == 0 ) {
                                            if( ter( point( x, y ) ) == t_floor_blue ) {
                                                ter_set( point( x, y ), t_rock_blue );
                                            } else if( ter( point( x, y ) ) == t_rock_blue ) {
                                                ter_set( point( x, y ), t_floor_blue );
                                            } else if( ter( point( x, y ) ) == t_floor_red ) {
                                                ter_set( point( x, y ), t_rock_red );
                                            } else if( ter( point( x, y ) ) == t_rock_red ) {
                                                ter_set( point( x, y ), t_floor_red );
                                            } else if( ter( point( x, y ) ) == t_floor_green ) {
                                                ter_set( point( x, y ), t_rock_green );
                                            } else if( ter( point( x, y ) ) == t_rock_green ) {
                                                ter_set( point( x, y ), t_floor_green );
                                            }
                                        }
                                        break;
                                }
                            }
                        }
                    }
                }
                break;

                case 4: { // Toggling walls maze
                    square( this, t_rock, point_zero, point( SEEX     - 1, 1 ) );
                    square( this, t_rock, point( 0, SEEY * 2 - 2 ), point( SEEX     - 1, SOUTH_EDGE ) );
                    square( this, t_rock, point( 0, 2 ), point( SEEX     - 4, SEEY * 2 - 3 ) );
                    square( this, t_rock, point( SEEX + 2, 0 ), point( EAST_EDGE, 1 ) );
                    square( this, t_rock, point( SEEX + 2, SEEY * 2 - 2 ), point( EAST_EDGE, SOUTH_EDGE ) );
                    square( this, t_rock, point( SEEX + 5, 2 ), point( EAST_EDGE, SEEY * 2 - 3 ) );
                    int x = rng( SEEX - 1, SEEX + 2 ), y = 2;
                    std::vector<point> path; // Path, from end to start
                    while( x < SEEX - 1 || x > SEEX + 2 || y < SEEY * 2 - 2 ) {
                        static const std::vector<ter_id> terrains = {
                            t_floor_red, t_floor_green, t_floor_blue,
                        };
                        path.emplace_back( x, y );
                        ter_set( point( x, y ), random_entry( terrains ) );
                        if( y == SEEY * 2 - 2 ) {
                            if( x < SEEX - 1 ) {
                                x++;
                            } else if( x > SEEX + 2 ) {
                                x--;
                            }
                        } else {
                            std::vector<point> next;
                            for( int nx = x - 1; nx <= x + 1; nx++ ) {
                                for( int ny = y; ny <= y + 1; ny++ ) {
                                    if( ter( point( nx, ny ) ) == t_rock_floor ) {
                                        next.emplace_back( nx, ny );
                                    }
                                }
                            }
                            if( next.empty() ) {
                                break;
                            } else {
                                const point p = random_entry( next );
                                x = p.x;
                                y = p.y;
                            }
                        }
                    }
                    // Now go backwards through path (start to finish), toggling any tiles that need
                    bool toggle_red = false;
                    bool toggle_green = false;
                    bool toggle_blue = false;
                    for( int i = path.size() - 1; i >= 0; i-- ) {
                        if( ter( path[i] ) == t_floor_red ) {
                            toggle_green = !toggle_green;
                            if( toggle_red ) {
                                ter_set( path[i], t_rock_red );
                            }
                        } else if( ter( path[i] ) == t_floor_green ) {
                            toggle_blue = !toggle_blue;
                            if( toggle_green ) {
                                ter_set( path[i], t_rock_green );
                            }
                        } else if( ter( path[i] ) == t_floor_blue ) {
                            toggle_red = !toggle_red;
                            if( toggle_blue ) {
                                ter_set( path[i], t_rock_blue );
                            }
                        }
                    }
                    // Finally, fill in the rest with random tiles, and place toggle traps
                    for( int i = SEEX - 3; i <= SEEX + 4; i++ ) {
                        for( int j = 2; j <= SEEY * 2 - 2; j++ ) {
                            mtrap_set( this, point( i, j ), tr_temple_toggle );
                            if( ter( point( i, j ) ) == t_rock_floor ) {
                                static const std::vector<ter_id> terrains = {
                                    t_rock_red, t_rock_green, t_rock_blue,
                                    t_floor_red, t_floor_green, t_floor_blue,
                                };
                                ter_set( point( i, j ), random_entry( terrains ) );
                            }
                        }
                    }
                }
                break;
            } // Done with room type switch
            // Stairs down if we need them
            if( terrain_type == "temple_stairs" ) {
                line( this, t_stairs_down, point( SEEX, 0 ), point( SEEX + 1, 0 ) );
            }
            // Stairs at the south if dat.above() has stairs down.
            if( dat.above() == "temple_stairs" ) {
                line( this, t_stairs_up, point( SEEX, SOUTH_EDGE ), point( SEEX + 1, SOUTH_EDGE ) );
            }

        } // Done with underground-only stuff
    } else if( terrain_type == "temple_finale" ) {
        fill_background( this, t_rock );
        square( this, t_rock_floor, point( SEEX - 1, 1 ), point( SEEX + 2, 4 ) );
        square( this, t_rock_floor, point( SEEX, 5 ), point( SEEX + 1, SOUTH_EDGE ) );
        line( this, t_stairs_up, point( SEEX, SOUTH_EDGE ), point( SEEX + 1, SOUTH_EDGE ) );
        spawn_artifact( tripoint( rng( SEEX, SEEX + 1 ), rng( 2, 3 ), abs_sub.z ) );
        spawn_artifact( tripoint( rng( SEEX, SEEX + 1 ), rng( 2, 3 ), abs_sub.z ) );
        return;

    }
}

void map::draw_mine( mapgendata &dat )
{
    const oter_id &terrain_type = dat.terrain_type();
    if( terrain_type == "mine" || terrain_type == "mine_down" ) {
        if( is_ot_match( "mine", dat.north(), ot_match_type::prefix ) ) {
            dat.n_fac = ( one_in( 10 ) ? 0 : -2 );
        } else {
            dat.n_fac = 4;
        }
        if( is_ot_match( "mine", dat.east(), ot_match_type::prefix ) ) {
            dat.e_fac = ( one_in( 10 ) ? 0 : -2 );
        } else {
            dat.e_fac = 4;
        }
        if( is_ot_match( "mine", dat.south(), ot_match_type::prefix ) ) {
            dat.s_fac = ( one_in( 10 ) ? 0 : -2 );
        } else {
            dat.s_fac = 4;
        }
        if( is_ot_match( "mine", dat.west(), ot_match_type::prefix ) ) {
            dat.w_fac = ( one_in( 10 ) ? 0 : -2 );
        } else {
            dat.w_fac = 4;
        }

        for( int i = 0; i < SEEX * 2; i++ ) {
            for( int j = 0; j < SEEY * 2; j++ ) {
                int i_reverse = SEEX * 2 - 1 - i;
                int j_reverse = SEEY * 2 - 1 - j;
                if( i >= dat.w_fac + rng( 0, 2 ) && i <= EAST_EDGE - dat.e_fac - rng( 0, 2 ) &&
                    j >= dat.n_fac + rng( 0, 2 ) && j <= SOUTH_EDGE - dat.s_fac - rng( 0, 2 ) &&
                    i + j >= 3 && i_reverse + j_reverse >= 3 &&
                    i + j_reverse >= 3 && j + i_reverse >= 3 ) {
                    ter_set( point( i, j ), t_rock_floor );
                } else {
                    ter_set( point( i, j ), t_rock );
                }
            }
        }

        // Not an entrance; maybe some hazards!
        switch( rng( 0, 4 ) ) {
            case 0:
                break; // Nothing!  Lucky!

            case 1: {
                // Toxic gas
                point gas_vent_location( rng( 9, 14 ), rng( 9, 14 ) );
                ter_set( point( gas_vent_location ), t_rock );
                add_field( { gas_vent_location, abs_sub.z }, fd_gas_vent, 2 );
            }
            break;

            case 2: {
                // Lava
                point start_location( rng( 6, SEEX ), rng( 6, SEEY ) );
                point end_location( rng( SEEX + 1, SEEX * 2 - 7 ), rng( SEEY + 1, SEEY * 2 - 7 ) );
                const int num = rng( 2, 4 );
                for( int i = 0; i < num; i++ ) {
                    int lx1 = start_location.x + rng( -1, 1 );
                    int lx2 = end_location.x + rng( -1, 1 );
                    int ly1 = start_location.y + rng( -1, 1 );
                    int ly2 = end_location.y + rng( -1, 1 );
                    line( this, t_lava, point( lx1, ly1 ), point( lx2, ly2 ) );
                }
                for( const tripoint &ore : points_in_rectangle( tripoint( start_location, abs_sub.z ),
                        tripoint( end_location,
                                  abs_sub.z ) ) ) {
                    if( ter( ore ) == t_rock_floor && one_in( 10 ) ) {
                        spawn_item( ore, "chunk_sulfur" );
                    }
                }
            }
            break;

            case 3: {
                // Wrecked equipment
                point wreck_location( rng( 9, 14 ), rng( 9, 14 ) );
                for( int i = wreck_location.x - 3; i < wreck_location.x + 3; i++ ) {
                    for( int j = wreck_location.y - 3; j < wreck_location.y + 3; j++ ) {
                        if( !one_in( 4 ) ) {
                            make_rubble( tripoint( i, j, abs_sub.z ), f_wreckage );
                        }
                    }
                }
            }
            break;

            case 4: {
                // Dead miners
                const int num_bodies = rng( 4, 8 );
                for( int i = 0; i < num_bodies; i++ ) {
                    if( const auto body = random_point( *this, [this]( const tripoint & p ) {
                    return move_cost( p ) == 2;
                    } ) ) {
                        add_item( *body, item::make_corpse() );
                        place_items( item_group_id( "mon_zombie_miner_death_drops" ), 100, *body, *body,
                                     false, calendar::start_of_cataclysm );
                    }
                }
            }
            break;

        }

        if( terrain_type == "mine_down" ) { // Don't forget to build a slope down!
            std::vector<direction> open;
            if( dat.n_fac == 4 ) {
                open.push_back( direction::NORTH );
            }
            if( dat.e_fac == 4 ) {
                open.push_back( direction::EAST );
            }
            if( dat.s_fac == 4 ) {
                open.push_back( direction::SOUTH );
            }
            if( dat.w_fac == 4 ) {
                open.push_back( direction::WEST );
            }

            if( open.empty() ) { // We'll have to build it in the center
                int tries = 0;
                point p;
                bool okay = true;
                do {
                    p.x = rng( SEEX - 6, SEEX + 1 );
                    p.y = rng( SEEY - 6, SEEY + 1 );
                    okay = true;
                    for( int i = p.x; ( i <= p.x + 5 ) && okay; i++ ) {
                        for( int j = p.y; ( j <= p.y + 5 ) && okay; j++ ) {
                            if( ter( point( i, j ) ) != t_rock_floor ) {
                                okay = false;
                            }
                        }
                    }
                    if( !okay ) {
                        tries++;
                    }
                } while( !okay && tries < 10 );
                if( tries == 10 ) { // Clear the area around the slope down
                    square( this, t_rock_floor, p, p + point( 5, 5 ) );
                }
                // NOLINTNEXTLINE(cata-use-named-point-constants)
                square( this, t_slope_down, p + point( 1, 1 ), p + point( 2, 2 ) );
            } else { // We can build against a wall
                switch( random_entry( open ) ) {
                    case direction::NORTH:
                        square( this, t_rock_floor, point( SEEX - 3, 6 ), point( SEEX + 2, SEEY ) );
                        line( this, t_slope_down, point( SEEX - 2, 6 ), point( SEEX + 1, 6 ) );
                        break;
                    case direction::EAST:
                        square( this, t_rock_floor, point( SEEX + 1, SEEY - 3 ), point( SEEX * 2 - 7, SEEY + 2 ) );
                        line( this, t_slope_down, point( SEEX * 2 - 7, SEEY - 2 ), point( SEEX * 2 - 7, SEEY + 1 ) );
                        break;
                    case direction::SOUTH:
                        square( this, t_rock_floor, point( SEEX - 3, SEEY + 1 ), point( SEEX + 2, SEEY * 2 - 7 ) );
                        line( this, t_slope_down, point( SEEX - 2, SEEY * 2 - 7 ), point( SEEX + 1, SEEY * 2 - 7 ) );
                        break;
                    case direction::WEST:
                        square( this, t_rock_floor, point( 6, SEEY - 3 ), point( SEEX, SEEY + 2 ) );
                        line( this, t_slope_down, point( 6, SEEY - 2 ), point( 6, SEEY + 1 ) );
                        break;
                    default:
                        break;
                }
            }
        } // Done building a slope down

        if( dat.above() == "mine_down" ) { // Don't forget to build a slope up!
            std::vector<direction> open;
            if( dat.n_fac == 6 && ter( point( SEEX, 6 ) ) != t_slope_down ) {
                open.push_back( direction::NORTH );
            }
            if( dat.e_fac == 6 && ter( point( SEEX * 2 - 7, SEEY ) ) != t_slope_down ) {
                open.push_back( direction::EAST );
            }
            if( dat.s_fac == 6 && ter( point( SEEX, SEEY * 2 - 7 ) ) != t_slope_down ) {
                open.push_back( direction::SOUTH );
            }
            if( dat.w_fac == 6 && ter( point( 6, SEEY ) ) != t_slope_down ) {
                open.push_back( direction::WEST );
            }

            if( open.empty() ) { // We'll have to build it in the center
                int tries = 0;
                point p;
                bool okay = true;
                do {
                    p.x = rng( SEEX - 6, SEEX + 1 );
                    p.y = rng( SEEY - 6, SEEY + 1 );
                    okay = true;
                    for( int i = p.x; ( i <= p.x + 5 ) && okay; i++ ) {
                        for( int j = p.y; ( j <= p.y + 5 ) && okay; j++ ) {
                            if( ter( point( i, j ) ) != t_rock_floor ) {
                                okay = false;
                            }
                        }
                    }
                    if( !okay ) {
                        tries++;
                    }
                } while( !okay && tries < 10 );
                if( tries == 10 ) { // Clear the area around the slope down
                    square( this, t_rock_floor, p, p + point( 5, 5 ) );
                }
                // NOLINTNEXTLINE(cata-use-named-point-constants)
                square( this, t_slope_up, p + point( 1, 1 ), p + point( 2, 2 ) );

            } else { // We can build against a wall
                switch( random_entry( open ) ) {
                    case direction::NORTH:
                        line( this, t_slope_up, point( SEEX - 2, 6 ), point( SEEX + 1, 6 ) );
                        break;
                    case direction::EAST:
                        line( this, t_slope_up, point( SEEX * 2 - 7, SEEY - 2 ), point( SEEX * 2 - 7, SEEY + 1 ) );
                        break;
                    case direction::SOUTH:
                        line( this, t_slope_up, point( SEEX - 2, SEEY * 2 - 7 ), point( SEEX + 1, SEEY * 2 - 7 ) );
                        break;
                    case direction::WEST:
                        line( this, t_slope_up, point( 6, SEEY - 2 ), point( 6, SEEY + 1 ) );
                        break;
                    default:
                        break;
                }
            }
        } // Done building a slope up
    } else if( terrain_type == "mine_finale" ) {
        // Set up the basic chamber
        for( int i = 0; i < SEEX * 2; i++ ) {
            for( int j = 0; j < SEEY * 2; j++ ) {
                if( i > rng( 1, 3 ) && i < SEEX * 2 - rng( 2, 4 ) &&
                    j > rng( 1, 3 ) && j < SEEY * 2 - rng( 2, 4 ) ) {
                    ter_set( point( i, j ), t_rock_floor );
                } else {
                    ter_set( point( i, j ), t_rock );
                }
            }
        }

        // Now draw the entrance(s)
        if( dat.north() == "mine" ) {
            square( this, t_rock_floor, point( SEEX, 0 ), point( SEEX + 1, 3 ) );
        }

        if( dat.east() == "mine" ) {
            square( this, t_rock_floor, point( SEEX * 2 - 4, SEEY ), point( EAST_EDGE, SEEY + 1 ) );
        }

        if( dat.south() == "mine" ) {
            square( this, t_rock_floor, point( SEEX, SEEY * 2 - 4 ), point( SEEX + 1, SOUTH_EDGE ) );
        }

        if( dat.west() == "mine" ) {
            square( this, t_rock_floor, point( 0, SEEY ), point( 3, SEEY + 1 ) );
        }

        // Now, pick and generate a type of finale!
        // The Thing dog
        const int num_bodies = rng( 4, 8 );
        for( int i = 0; i < num_bodies; i++ ) {
            point p3( rng( 4, SEEX * 2 - 5 ), rng( 4, SEEX * 2 - 5 ) );
            add_item( p3, item::make_corpse() );
            place_items( item_group_id( "mon_zombie_miner_death_drops" ), 60, p3,
                         p3, false, calendar::start_of_cataclysm );
        }
        place_spawns( GROUP_DOG_THING, 1, point( SEEX, SEEX ), point( SEEX + 1, SEEX + 1 ), 1, true, true );
        spawn_artifact( tripoint( rng( SEEX, SEEX + 1 ), rng( SEEY, SEEY + 1 ), abs_sub.z ) );
    }
}

void map::draw_slimepit( mapgendata &dat )
{
    const oter_id &terrain_type = dat.terrain_type();
    if( is_ot_match( "slimepit", terrain_type, ot_match_type::prefix ) ) {
        if( dat.zlevel() == 0 ) {
            dat.fill_groundcover();
        } else {
            draw_fill_background( t_rock_floor );
        }

        for( int i = 0; i < 4; i++ ) {
            if( !is_ot_match( "slimepit", dat.t_nesw[i], ot_match_type::prefix ) ) {
                dat.set_dir( i, SEEX );
            }
        }

        for( int i = 0; i < SEEX * 2; i++ ) {
            for( int j = 0; j < SEEY * 2; j++ ) {
                if( !one_in( 5 ) && ( rng( 0, dat.n_fac ) <= j &&
                                      rng( 0, dat.w_fac ) <= i &&
                                      SEEY * 2 - rng( 0, dat.s_fac ) > j &&
                                      SEEX * 2 - rng( 0, dat.e_fac ) > i ) ) {
                    ter_set( point( i, j ), t_slime );
                }
            }
        }
        if( terrain_type == "slimepit_down" ) {
            ter_set( point( rng( 3, SEEX * 2 - 4 ), rng( 3, SEEY * 2 - 4 ) ), t_slope_down );
        }
        if( dat.above() == "slimepit_down" ) {
            switch( rng( 1, 4 ) ) {
                case 1:
                    ter_set( point( rng( 0, 2 ), rng( 0, 2 ) ), t_slope_up );
                    break;
                case 2:
                    ter_set( point( rng( 0, 2 ), SEEY * 2 - rng( 1, 3 ) ), t_slope_up );
                    break;
                case 3:
                    ter_set( point( SEEX * 2 - rng( 1, 3 ), rng( 0, 2 ) ), t_slope_up );
                    break;
                case 4:
                    ter_set( point( SEEX * 2 - rng( 1, 3 ), SEEY * 2 - rng( 1, 3 ) ), t_slope_up );
            }
        }
        place_spawns( GROUP_BLOB, 1, point( SEEX, SEEY ), point( SEEX, SEEY ), 0.15 );
        place_items( item_group_id( "sewer" ), 40, point_zero, point( EAST_EDGE, SOUTH_EDGE ), true,
                     calendar::start_of_cataclysm );
    }
}

void map::draw_connections( const mapgendata &dat )
{
    const oter_id &terrain_type = dat.terrain_type();
    if( is_ot_match( "subway", terrain_type,
                     ot_match_type::type ) ) { // FUUUUU it's IF ELIF ELIF ELIF's mini-me =[
        if( is_ot_match( "sewer", dat.north(), ot_match_type::type ) &&
            !connects_to( terrain_type, 0 ) ) {
            if( connects_to( dat.north(), 2 ) ) {
                for( int i = SEEX - 2; i < SEEX + 2; i++ ) {
                    for( int j = 0; j < SEEY; j++ ) {
                        ter_set( point( i, j ), t_sewage );
                    }
                }
            } else {
                for( int j = 0; j < 3; j++ ) {
                    ter_set( point( SEEX, j ), t_rock_floor );
                    ter_set( point( SEEX - 1, j ), t_rock_floor );
                }
                ter_set( point( SEEX, 3 ), t_door_metal_c );
                ter_set( point( SEEX - 1, 3 ), t_door_metal_c );
            }
        }
        if( is_ot_match( "sewer", dat.east(), ot_match_type::type ) &&
            !connects_to( terrain_type, 1 ) ) {
            if( connects_to( dat.east(), 3 ) ) {
                for( int i = SEEX; i < SEEX * 2; i++ ) {
                    for( int j = SEEY - 2; j < SEEY + 2; j++ ) {
                        ter_set( point( i, j ), t_sewage );
                    }
                }
            } else {
                for( int i = SEEX * 2 - 3; i < SEEX * 2; i++ ) {
                    ter_set( point( i, SEEY ), t_rock_floor );
                    ter_set( point( i, SEEY - 1 ), t_rock_floor );
                }
                ter_set( point( SEEX * 2 - 4, SEEY ), t_door_metal_c );
                ter_set( point( SEEX * 2 - 4, SEEY - 1 ), t_door_metal_c );
            }
        }
        if( is_ot_match( "sewer", dat.south(), ot_match_type::type ) &&
            !connects_to( terrain_type, 2 ) ) {
            if( connects_to( dat.south(), 0 ) ) {
                for( int i = SEEX - 2; i < SEEX + 2; i++ ) {
                    for( int j = SEEY; j < SEEY * 2; j++ ) {
                        ter_set( point( i, j ), t_sewage );
                    }
                }
            } else {
                for( int j = SEEY * 2 - 3; j < SEEY * 2; j++ ) {
                    ter_set( point( SEEX, j ), t_rock_floor );
                    ter_set( point( SEEX - 1, j ), t_rock_floor );
                }
                ter_set( point( SEEX, SEEY * 2 - 4 ), t_door_metal_c );
                ter_set( point( SEEX - 1, SEEY * 2 - 4 ), t_door_metal_c );
            }
        }
        if( is_ot_match( "sewer", dat.west(), ot_match_type::type ) &&
            !connects_to( terrain_type, 3 ) ) {
            if( connects_to( dat.west(), 1 ) ) {
                for( int i = 0; i < SEEX; i++ ) {
                    for( int j = SEEY - 2; j < SEEY + 2; j++ ) {
                        ter_set( point( i, j ), t_sewage );
                    }
                }
            } else {
                for( int i = 0; i < 3; i++ ) {
                    ter_set( point( i, SEEY ), t_rock_floor );
                    ter_set( point( i, SEEY - 1 ), t_rock_floor );
                }
                ter_set( point( 3, SEEY ), t_door_metal_c );
                ter_set( point( 3, SEEY - 1 ), t_door_metal_c );
            }
        }
    } else if( is_ot_match( "sewer", terrain_type, ot_match_type::type ) ) {
        if( dat.above() == "road_nesw_manhole" ) {
            ter_set( point( rng( SEEX - 2, SEEX + 1 ), rng( SEEY - 2, SEEY + 1 ) ), t_ladder_up );
        }
        if( is_ot_match( "subway", dat.north(), ot_match_type::type ) &&
            !connects_to( terrain_type, 0 ) ) {
            for( int j = 0; j < SEEY - 3; j++ ) {
                ter_set( point( SEEX, j ), t_rock_floor );
                ter_set( point( SEEX - 1, j ), t_rock_floor );
            }
            ter_set( point( SEEX, SEEY - 3 ), t_door_metal_c );
            ter_set( point( SEEX - 1, SEEY - 3 ), t_door_metal_c );
        }
        if( is_ot_match( "subway", dat.east(), ot_match_type::type ) &&
            !connects_to( terrain_type, 1 ) ) {
            for( int i = SEEX + 3; i < SEEX * 2; i++ ) {
                ter_set( point( i, SEEY ), t_rock_floor );
                ter_set( point( i, SEEY - 1 ), t_rock_floor );
            }
            ter_set( point( SEEX + 2, SEEY ), t_door_metal_c );
            ter_set( point( SEEX + 2, SEEY - 1 ), t_door_metal_c );
        }
        if( is_ot_match( "subway", dat.south(), ot_match_type::type ) &&
            !connects_to( terrain_type, 2 ) ) {
            for( int j = SEEY + 3; j < SEEY * 2; j++ ) {
                ter_set( point( SEEX, j ), t_rock_floor );
                ter_set( point( SEEX - 1, j ), t_rock_floor );
            }
            ter_set( point( SEEX, SEEY + 2 ), t_door_metal_c );
            ter_set( point( SEEX - 1, SEEY + 2 ), t_door_metal_c );
        }
        if( is_ot_match( "subway", dat.west(), ot_match_type::type ) &&
            !connects_to( terrain_type, 3 ) ) {
            for( int i = 0; i < SEEX - 3; i++ ) {
                ter_set( point( i, SEEY ), t_rock_floor );
                ter_set( point( i, SEEY - 1 ), t_rock_floor );
            }
            ter_set( point( SEEX - 3, SEEY ), t_door_metal_c );
            ter_set( point( SEEX - 3, SEEY - 1 ), t_door_metal_c );
        }
    }

    // finally, any terrain with SIDEWALKS should contribute sidewalks to neighboring diagonal roads
    if( terrain_type->has_flag( oter_flags::has_sidewalk ) ) {
        for( int dir = 4; dir < 8; dir++ ) { // NE SE SW NW
            bool n_roads_nesw[4] = {};
            int n_num_dirs = terrain_type_to_nesw_array( oter_id( dat.t_nesw[dir] ), n_roads_nesw );
            // only handle diagonal neighbors
            if( n_num_dirs == 2 &&
                n_roads_nesw[( ( dir - 4 ) + 3 ) % 4] &&
                n_roads_nesw[( ( dir - 4 ) + 2 ) % 4] ) {
                // make drawing simpler by rotating the map back and forth
                rotate( 4 - ( dir - 4 ) );
                // draw a small triangle of sidewalk in the northeast corner
                for( int y = 0; y < 4; y++ ) {
                    for( int x = SEEX * 2 - 4; x < SEEX * 2; x++ ) {
                        if( x - y > SEEX * 2 - 4 ) {
                            // TODO: more discriminating conditions
                            if( ter( point( x, y ) ) == t_grass || ter( point( x, y ) ) == t_dirt ||
                                ter( point( x, y ) ) == t_shrub ) {
                                ter_set( point( x, y ), t_sidewalk );
                            }
                        }
                    }
                }
                rotate( ( dir - 4 ) );
            }
        }
    }

    resolve_regional_terrain_and_furniture( dat );
}

void map::place_spawns( const mongroup_id &group, const int chance,
                        point p1, point p2, const float density,
                        const bool individual, const bool friendly, const std::string &name, const int mission_id )
{
    if( !group.is_valid() ) {
        // TODO: fix point types
        const tripoint_abs_omt omt( sm_to_omt_copy( get_abs_sub() ) );
        const oter_id &oid = overmap_buffer.ter( omt );
        debugmsg( "place_spawns: invalid mongroup '%s', om_terrain = '%s' (%s)", group.c_str(),
                  oid.id().c_str(), oid->get_mapgen_id().c_str() );
        return;
    }

    // Set chance to be 1 or less to guarantee spawn, else set higher than 1
    if( !one_in( chance ) ) {
        return;
    }

    float spawn_density = 1.0f;
    if( MonsterGroupManager::is_animal( group ) ) {
        spawn_density = get_option< float >( "SPAWN_ANIMAL_DENSITY" );
    } else {
        spawn_density = get_option< float >( "SPAWN_DENSITY" );
    }

    float multiplier = density * spawn_density;
    // Only spawn 1 creature if individual flag set, else scale by density
    float thenum = individual ? 1 : ( multiplier * rng_float( 10.0f, 50.0f ) );
    int num = roll_remainder( thenum );

    // GetResultFromGroup decrements num
    while( num > 0 ) {
        int tries = 10;
        point p;

        // Pick a spot for the spawn
        do {
            p.x = rng( p1.x, p2.x );
            p.y = rng( p1.y, p2.y );
            tries--;
        } while( impassable( p ) && tries > 0 );

        // Pick a monster type
        MonsterGroupResult spawn_details = MonsterGroupManager::GetResultFromGroup( group, &num );

        add_spawn( spawn_details.name, spawn_details.pack_size, { p, abs_sub.z },
                   friendly, -1, mission_id, name );
    }
}

void map::place_gas_pump( const point &p, int charges, const itype_id &fuel_type )
{
    detached_ptr<item> fuel = item::spawn( fuel_type, calendar::start_of_cataclysm );
    fuel->charges = charges;
    ter_set( p, ter_id( fuel->fuel_pump_terrain() ) );
    add_item( p, std::move( fuel ) );
}

void map::place_gas_pump( const point &p, int charges )
{
    place_gas_pump( p, charges, one_in( 4 ) ? itype_diesel : itype_gasoline );
}

void map::place_toilet( point p, int charges )
{
    detached_ptr<item> water = item::spawn( "water", calendar::start_of_cataclysm );
    water->charges = charges;
    add_item( p, std::move( water ) );
    furn_set( p, f_toilet );
}

void map::place_vending( point p, const item_group_id &type, bool reinforced )
{
    if( reinforced ) {
        furn_set( p, f_vending_reinforced );
        place_items( type, 100, p, p, false, calendar::start_of_cataclysm );
    } else {
        if( one_in( 2 ) ) {
            furn_set( p, f_vending_o );
            for( const auto &loc : points_in_radius( { p, abs_sub.z }, 1 ) ) {
                if( one_in( 4 ) ) {
                    spawn_item( loc, "glass_shard", rng( 1, 2 ) );
                }
            }
        } else {
            furn_set( p, f_vending_c );
            place_items( type, 100, p, p, false, calendar::start_of_cataclysm );
        }
    }
}

character_id map::place_npc( point p, const string_id<npc_template> &type, bool force )
{
    if( !force && !get_option<bool>( "STATIC_NPC" ) ) {
        return character_id(); //Do not generate an npc.
    }
    shared_ptr_fast<npc> temp = make_shared_fast<npc>();
    temp->load_npc_template( type );
    temp->spawn_at_precise( { abs_sub.xy() }, { p, abs_sub.z } );
    temp->toggle_trait( trait_NPC_STATIC_NPC );
    overmap_buffer.insert_npc( temp );
    return temp->getID();
}

void map::apply_faction_ownership( point p1, point p2, const faction_id &id )
{
    for( const tripoint &p : points_in_rectangle( tripoint( p1, abs_sub.z ), tripoint( p2,
            abs_sub.z ) ) ) {
        auto items = i_at( p.xy() );
        for( item * const &elem : items ) {
            elem->set_owner( id );
        }
        vehicle *source_veh = veh_pointer_or_null( veh_at( p ) );
        if( source_veh ) {
            if( !source_veh->has_owner() ) {
                source_veh->set_owner( id );
            }
        }
    }
}

// A chance of 100 indicates that items should always spawn,
// the item group should be responsible for determining the amount of items.
std::vector<item *> map::place_items( const item_group_id &loc, const int chance,
                                      const tripoint &p1,
                                      const tripoint &p2, const bool ongrass, const time_point &turn,
                                      const int magazine, const int ammo )
{
    // TODO: implement for 3D
    std::vector<item *> res;

    if( chance > 100 || chance <= 0 ) {
        debugmsg( "map::place_items() called with an invalid chance (%d)", chance );
        return res;
    }
    if( !item_group::group_is_defined( loc ) ) {
        // TODO: fix point types
        const tripoint_abs_omt omt( sm_to_omt_copy( get_abs_sub() ) );
        const oter_id &oid = overmap_buffer.ter( omt );
        debugmsg( "place_items: invalid item group '%s', om_terrain = '%s' (%s)",
                  loc.c_str(), oid.id().c_str(), oid->get_mapgen_id().c_str() );
        return res;
    }

    const float spawn_rate = get_option<float>( "ITEM_SPAWNRATE" );
    const int spawn_count = roll_remainder( chance * spawn_rate / 100.0f );

    for( int i = 0; i < spawn_count; i++ ) {
        int tries = 0;
        auto is_valid_terrain = [this, ongrass]( const tripoint & p ) {
            const ter_t &terrain = ter( p ).obj();
            return terrain.movecost == 0 &&
                   !terrain.has_flag( "PLACE_ITEM" ) &&
                   !ongrass &&
                   !terrain.has_flag( "FLAT" );
        };

        tripoint p;
        do {
            p.x = rng( p1.x, p2.x );
            p.y = rng( p1.y, p2.y );
            p.z = abs_sub.z;
            tries++;
        } while( is_valid_terrain( p ) && tries < 20 );

        if( tries < 20 ) {
            std::vector<detached_ptr<item>> initial = item_group::items_from( loc, turn );

            for( detached_ptr<item> &itm : initial ) {
                const float cat_rate = item_category_spawn_rate( *itm );

                if( cat_rate <= 1.0f ) {
                    if( rng_float( 0.1f, 1.0f ) <= cat_rate ) {
                        detached_ptr<item> placed = add_item_or_charges( p, std::move( itm ) );
                        if( placed ) {
                            res.push_back( std::move( &*placed ) );
                        }
                    }
                } else {
                    const item &real_item = *itm; // original item reference

                    // Spawn the base item once (as before)
                    detached_ptr<item> placed = add_item_or_charges( p, std::move( itm ) );
                    if( placed ) {
                        res.push_back( std::move( &*placed ) );
                    }

                    // Build the list of extra items of the same category
                    std::vector<detached_ptr<item>> extra = item_group::items_from( loc, turn );
                    extra.erase(
                        std::remove_if(
                            extra.begin(), extra.end(),
                    [&real_item]( const detached_ptr<item> &it ) {
                        return item_category_id( it->get_category_id() )
                               != item_category_id( real_item.get_category_id() );
                    }
                        ),
                    extra.end()
                    );

                    // Spawn additional items randomly rather than all
                    int base_count = static_cast<int>( cat_rate );
                    for( int i = 0; i < base_count; i++ ) {
                        if( extra.empty() ) {
                            break;
                        }
                        int idx = rng( 0, static_cast<int>( extra.size() ) - 1 );
                        detached_ptr<item> spawned = add_item_or_charges( p, std::move( extra[idx] ) );
                        if( spawned ) {
                            res.push_back( std::move( &*spawned ) );
                        }
                    }

                    // Handle fractional part (e.g. cat_rate = 2.7 => 70% chance of one more)
                    if( rng_float( 0.0f, 1.0f ) < ( cat_rate - static_cast<float>( base_count ) ) ) {
                        if( !extra.empty() ) {
                            int idx = rng( 0, static_cast<int>( extra.size() ) - 1 );
                            detached_ptr<item> spawned = add_item_or_charges( p, std::move( extra[idx] ) );
                            if( spawned ) {
                                res.push_back( std::move( &*spawned ) );
                            }
                        }
                    }
                }
            }
        }
    }


    for( auto e : res ) {
        if( e->is_tool() || e->is_gun() || e->is_magazine() ) {
            if( rng( 0, 99 ) < magazine && !e->magazine_current() &&
                e->magazine_default() != itype_id::NULL_ID() ) {
                e->put_in( item::spawn( e->magazine_default(), e->birthday() ) );
            }
            if( rng( 0, 99 ) < ammo && e->ammo_remaining() == 0 ) {
                e->ammo_set( e->ammo_default(), e->ammo_capacity() );
            }
        }
    }
    return res;
}

std::vector<item *> map::put_items_from_loc( const item_group_id &loc, const tripoint &p,
        const time_point &turn )
{
    std::vector<detached_ptr<item>> items = item_group::items_from( loc, turn );
    std::vector<item *> ret;
    ret.reserve( items.size() );
    for( detached_ptr<item> &it : items ) {
        ret.push_back( &*it );
    }
    spawn_items( p, std::move( items ) );
    return ret;
}

void map::add_spawn( const mtype_id &type, int count, const tripoint &p, bool friendly,
                     int faction_id, int mission_id, const std::string &name ) const
{
    add_spawn( type, count, p, spawn_point::friendly_to_spawn_disposition( friendly ), faction_id,
               mission_id, name );
}

void map::add_spawn( const mtype_id &type, int count, const tripoint &p,
                     spawn_disposition disposition,
                     int faction_id, int mission_id, const std::string &name ) const
{
    if( p.x < 0 || p.x >= SEEX * my_MAPSIZE || p.y < 0 || p.y >= SEEY * my_MAPSIZE ) {
        debugmsg( "Bad add_spawn(%s, %d, %d, %d)", type.c_str(), count, p.x, p.y );
        return;
    }
    point offset;
    submap *place_on_submap = get_submap_at( p, offset );

    if( !place_on_submap ) {
        debugmsg( "centadodecamonant doesn't exist in grid; within add_spawn(%s, %d, %d, %d, %d)",
                  type.c_str(), count, p.x, p.y, p.z );
        return;
    }
    if( MonsterGroupManager::monster_is_blacklisted( type ) ) {
        return;
    }
    spawn_point tmp( type, count, offset, faction_id, mission_id, disposition, name );
    place_on_submap->spawns.push_back( tmp );
}

vehicle *map::add_vehicle( const vgroup_id &type, const tripoint &p, const units::angle dir,
                           const int veh_fuel, const int veh_status, const bool merge_wrecks )
{
    return add_vehicle( type.obj().pick(), p, dir, veh_fuel, veh_status, merge_wrecks );
}

vehicle *map::add_vehicle( const vgroup_id &type, point p, units::angle dir,
                           int veh_fuel, int veh_status, bool merge_wrecks )
{
    return add_vehicle( type.obj().pick(), p, dir, veh_fuel, veh_status, merge_wrecks );
}

vehicle *map::add_vehicle( const vproto_id &type, point p, units::angle dir,
                           int veh_fuel, int veh_status, bool merge_wrecks )
{
    return add_vehicle( type, tripoint( p, abs_sub.z ), dir, veh_fuel, veh_status, merge_wrecks );
}

vehicle *map::add_vehicle( const vproto_id &type, const tripoint &p, const units::angle dir,
                           const int veh_fuel, const int veh_status, const bool merge_wrecks )
{
    if( !type.is_valid() ) {
        debugmsg( "Nonexistent vehicle type: \"%s\"", type.c_str() );
        return nullptr;
    }
    if( !inbounds( p ) ) {
        dbg( DL::Warn ) << string_format( "Out of bounds add_vehicle t=%s d=%d p=%s",
                                          type, to_degrees( dir ), p.to_string() );
        return nullptr;
    }

    // debugmsg("n=%d x=%d y=%d MAPSIZE=%d ^2=%d", nonant, x, y, MAPSIZE, MAPSIZE*MAPSIZE);
    auto veh = std::make_unique<vehicle>( type, veh_fuel, veh_status );
    tripoint p_ms = p;
    veh->sm_pos = ms_to_sm_remain( p_ms );
    veh->pos = p_ms.xy();
    veh->place_spawn_items();
    // for backwards compatibility, we always spawn with a pivot point of (0,0) so
    // that the mount at (0,0) is located at the spawn position.
    veh->set_facing_and_pivot( dir, point_zero, false );
    //debugmsg("adding veh: %d, sm: %d,%d,%d, pos: %d, %d", veh, veh->smx, veh->smy, veh->smz, veh->posx, veh->posy);
    std::unique_ptr<vehicle> placed_vehicle_up =
        add_vehicle_to_map( std::move( veh ), merge_wrecks );
    vehicle *placed_vehicle = placed_vehicle_up.get();

    if( placed_vehicle != nullptr ) {
        submap *place_on_submap = get_submap_at_grid( placed_vehicle->sm_pos );
        place_on_submap->vehicles.push_back( std::move( placed_vehicle_up ) );
        place_on_submap->is_uniform = false;
        invalidate_max_populated_zlev( p.z );

        auto &ch = get_cache( placed_vehicle->sm_pos.z );
        ch.vehicle_list.insert( placed_vehicle );
        add_vehicle_to_cache( placed_vehicle );

        //debugmsg ("grid[%d]->vehicles.size=%d veh.parts.size=%d", nonant, grid[nonant]->vehicles.size(),veh.parts.size());
    }
    return placed_vehicle;
}

/**
 * Takes a vehicle already created with new and attempts to place it on the map,
 * checking for collisions. If the vehicle can't be placed, returns NULL,
 * otherwise returns a pointer to the placed vehicle, which may not necessarily
 * be the one passed in (if wreckage is created by fusing cars).
 * @param veh The vehicle to place on the map.
 * @param merge_wrecks Whether crashed vehicles become part of each other
 * @return The vehicle that was finally placed.
 */
std::unique_ptr<vehicle> map::add_vehicle_to_map(
    std::unique_ptr<vehicle> veh, const bool merge_wrecks )
{
    //We only want to check once per square, so loop over all structural parts
    std::vector<int> frame_indices = veh->all_parts_at_location( "structure" );

    //Check for boat type vehicles that should be placeable in deep water
    const bool can_float = size( veh->get_avail_parts( "FLOATS" ) ) > 2;

    //When hitting a wall, only smash the vehicle once (but walls many times)
    bool needs_smashing = false;

    veh->attach();
    veh->refresh_position();

    for( std::vector<int>::const_iterator part = frame_indices.begin();
         part != frame_indices.end(); part++ ) {
        const auto p = veh->global_part_pos3( *part );

        //Don't spawn anything in water
        if( has_flag_ter( TFLAG_DEEP_WATER, p ) && !can_float ) {
            return nullptr;
        }

        // Don't spawn shopping carts on top of another vehicle or other obstacle.
        if( veh->type == vproto_id( "shopping_cart" ) ) {
            if( veh_at( p ) || impassable( p ) ) {
                return nullptr;
            }
        }

        //For other vehicles, simulate collisions with (non-shopping cart) stuff
        vehicle *const other_veh = veh_pointer_or_null( veh_at( p ) );
        if( other_veh != nullptr && other_veh->type != vproto_id( "shopping_cart" ) ) {
            if( !merge_wrecks ) {
                return nullptr;
            }

            // Hard wreck-merging limit: 200 tiles
            // Merging is slow for big vehicles which lags the mapgen
            if( frame_indices.size() + other_veh->all_parts_at_location( "structure" ).size() > 200 ) {
                return nullptr;
            }

            /* There's a vehicle here, so let's fuse them together into wreckage and
             * smash them up. It'll look like a nasty collision has occurred.
             * Trying to do a local->global->local conversion would be a major
             * headache, so instead, let's make another vehicle whose (0, 0) point
             * is the (0, 0) of the existing vehicle, convert the coordinates of both
             * vehicles into global coordinates, find the distance between them and
             * p and then install them that way.
             * Create a vehicle with type "null" so it starts out empty. */
            auto wreckage = std::make_unique<vehicle>();
            wreckage->pos = other_veh->pos;
            wreckage->sm_pos = other_veh->sm_pos;

            //Where are we on the global scale?
            const tripoint global_pos = wreckage->global_pos3();

            // We must remove the vehicle from the map before we move away its parts
            std::unique_ptr<vehicle> old_veh = detach_vehicle( other_veh );

            for( const vpart_reference &vpr : veh->get_all_parts() ) {
                const tripoint part_pos = veh->global_part_pos3( vpr.part() ) - global_pos;
                // TODO: change mount points to be tripoint
                wreckage->install_part( part_pos.xy(), std::move( vpr.part() ) );
            }

            for( const vpart_reference &vpr : old_veh->get_all_parts() ) {
                const tripoint part_pos = old_veh->global_part_pos3( vpr.part() ) - global_pos;
                wreckage->install_part( part_pos.xy(), vehicle_part{vpr.part(), &*wreckage} );
            }

            wreckage->name = _( "Wreckage" );


            // Try again with the wreckage
            std::unique_ptr<vehicle> new_veh = add_vehicle_to_map( std::move( wreckage ), true );
            if( new_veh != nullptr ) {
                new_veh->smash( *this );
                return new_veh;
            }

            // If adding the wreck failed, we want to restore the vehicle we tried to merge with
            add_vehicle_to_map( std::move( old_veh ), false );
            return nullptr;

        } else if( impassable( p ) ) {
            if( !merge_wrecks ) {
                return nullptr;
            }

            // There's a wall or other obstacle here; destroy it
            destroy( p, true );

            // Some weird terrain, don't place the vehicle
            if( impassable( p ) ) {
                return nullptr;
            }

            needs_smashing = true;
        }
    }

    if( needs_smashing ) {
        veh->smash( *this );
    }

    return veh;
}

computer *map::add_computer( const tripoint &p, const std::string &name, int security )
{
    // TODO: Turn this off?
    ter_set( p, t_console );
    point l;
    submap *const place_on_submap = get_submap_at( p, l );
    place_on_submap->set_computer( l, computer( name, security ) );
    return place_on_submap->get_computer( l );
}

/**
 * Rotates this map, and all of its contents, by the specified multiple of 90
 * degrees.
 * @param turns How many 90-degree turns to rotate the map.
 */
void map::rotate( int turns, const bool setpos_safe )
{

    //Handle anything outside the 1-3 range gracefully; rotate(0) is a no-op.
    turns = turns % 4;
    if( turns == 0 ) {
        return;
    }

    real_coords rc;
    const tripoint &abs_sub = get_abs_sub();
    rc.fromabs( point( abs_sub.x * SEEX, abs_sub.y * SEEY ) );

    // TODO: This radius can be smaller - how small?
    const int radius = HALF_MAPSIZE + 3;
    // uses submap coordinates
    // TODO: fix point types
    const std::vector<shared_ptr_fast<npc>> npcs =
            overmap_buffer.get_npcs_near( tripoint_abs_sm( abs_sub ), radius );
    for( const shared_ptr_fast<npc> &i : npcs ) {
        npc &np = *i;
        const tripoint sq = np.global_square_location();
        const point local_sq = getlocal( sq ).xy();

        real_coords np_rc;
        np_rc.fromabs( sq.xy() );
        // Note: We are rotating the entire overmap square (2x2 of submaps)
        if( np_rc.om_pos != rc.om_pos || sq.z != abs_sub.z ) {
            continue;
        }

        // OK, this is ugly: we remove the NPC from the whole map
        // Then we place it back from scratch
        // It could be rewritten to utilize the fact that rotation shouldn't cross overmaps

        point old( np_rc.sub_pos );
        if( np_rc.om_sub.x % 2 != 0 ) {
            old.x += SEEX;
        }
        if( np_rc.om_sub.y % 2 != 0 ) {
            old.y += SEEY;
        }

        const point new_pos = old .rotate( turns, { SEEX * 2, SEEY * 2 } );
        if( setpos_safe ) {
            // setpos can't be used during mapgen, but spawn_at_precise clips position
            // to be between 0-11,0-11 and teleports NPCs when used inside of update_mapgen
            // calls
            const tripoint new_global_sq = sq - local_sq + new_pos;
            np.setpos( get_map().getlocal( new_global_sq ) );
        } else {
            // OK, this is ugly: we remove the NPC from the whole map
            // Then we place it back from scratch
            // It could be rewritten to utilize the fact that rotation shouldn't cross overmaps
            shared_ptr_fast<npc> npc_ptr = overmap_buffer.remove_npc( np.getID() );
            np.spawn_at_precise( { abs_sub.xy() }, { new_pos, abs_sub.z } );
            overmap_buffer.insert_npc( npc_ptr );
        }
    }

    clear_vehicle_cache( );
    clear_vehicle_list( abs_sub.z );

    // Move the submaps around.
    // 2,2 <-> 1,1
    // 1,1
    //
    auto swap_submaps = [&]( const point & p1, const point & p2 ) {

        submap *sm1 = get_submap_at_grid( p1 );
        submap *sm2 = get_submap_at_grid( p2 );
        submap::swap( *sm1, *sm2 );

    };

    if( turns == 2 ) {
        swap_submaps( point_zero, point_south_east );
        swap_submaps( point_south, point_east );
    } else {
        point p;
        point p2 = p.rotate( turns, {2, 2} );
        point p3 = p2.rotate( turns, {2, 2} );
        point p4 = p3.rotate( turns, {2, 2} );

        swap_submaps( p, p2 );
        swap_submaps( p, p3 );
        swap_submaps( p, p4 );
    }

    // Then rotate them and recalculate vehicle positions.
    for( int j = 0; j < 2; ++j ) {
        for( int i = 0; i < 2; ++i ) {
            point p( i, j );
            auto sm = get_submap_at_grid( p );

            sm->rotate( turns );

            for( auto &veh : sm->vehicles ) {
                veh->sm_pos = tripoint( p, abs_sub.z );
            }

            update_vehicle_list( sm, abs_sub.z );
        }
    }
    reset_vehicle_cache( );

    // rotate zones
    zone_manager &mgr = zone_manager::get_manager();
    mgr.rotate_zones( *this, turns );
}

// Hideous function, I admit...
bool connects_to( const oter_id &there, int dir )
{
    switch( dir ) {
        // South
        case 2:
            return there == "sewer_ns"   || there == "sewer_es" || there == "sewer_sw" ||
                   there == "sewer_nes"  || there == "sewer_nsw" || there == "sewer_esw" ||
                   there == "sewer_nesw" || there == "ants_ns" || there == "ants_es" ||
                   there == "ants_sw"    || there == "ants_nes" ||  there == "ants_nsw" ||
                   there == "ants_esw"   || there == "ants_nesw";
        // West
        case 3:
            return there == "sewer_ew"   || there == "sewer_sw" || there == "sewer_wn" ||
                   there == "sewer_new"  || there == "sewer_nsw" || there == "sewer_esw" ||
                   there == "sewer_nesw" || there == "ants_ew" || there == "ants_sw" ||
                   there == "ants_wn"    || there == "ants_new" || there == "ants_nsw" ||
                   there == "ants_esw"   || there == "ants_nesw";
        // North
        case 0:
            return there == "sewer_ns"   || there == "sewer_ne" ||  there == "sewer_wn" ||
                   there == "sewer_nes"  || there == "sewer_new" || there == "sewer_nsw" ||
                   there == "sewer_nesw" || there == "ants_ns" || there == "ants_ne" ||
                   there == "ants_wn"    || there == "ants_nes" || there == "ants_new" ||
                   there == "ants_nsw"   || there == "ants_nesw";
        // East
        case 1:
            return there == "sewer_ew"   || there == "sewer_ne" || there == "sewer_es" ||
                   there == "sewer_nes"  || there == "sewer_new" || there == "sewer_esw" ||
                   there == "sewer_nesw" || there == "ants_ew" || there == "ants_ne" ||
                   there == "ants_es"    || there == "ants_nes" || there == "ants_new" ||
                   there == "ants_esw"   || there == "ants_nesw";
        default:
            debugmsg( "Connects_to with dir of %d", dir );
            return false;
    }
}

void science_room( map *m, const point &p1, const point &p2, int z, int rotate )
{
    int height = p2.y - p1.y;
    int width  = p2.x - p1.x;
    if( rotate % 2 == 1 ) { // Swap width & height if we're a lateral room
        int tmp = height;
        height  = width;
        width   = tmp;
    }
    for( int i = p1.x; i <= p2.x; i++ ) {
        for( int j = p1.y; j <= p2.y; j++ ) {
            m->ter_set( point( i, j ), t_thconc_floor );
        }
    }
    int area = height * width;
    std::vector<room_type> valid_rooms;
    if( height < 5 && width < 5 ) {
        valid_rooms.push_back( room_closet );
    }
    if( height > 6 && width > 3 ) {
        valid_rooms.push_back( room_lobby );
    }
    if( height > 4 || width > 4 ) {
        valid_rooms.push_back( room_chemistry );
        valid_rooms.push_back( room_goo );
    }
    if( z != 0 && ( height > 7 || width > 7 ) && height > 2 && width > 2 ) {
        valid_rooms.push_back( room_teleport );
    }
    if( height > 7 && width > 7 ) {
        valid_rooms.push_back( room_bionics );
        valid_rooms.push_back( room_cloning );
    }
    if( area >= 9 ) {
        valid_rooms.push_back( room_vivisect );
    }
    if( height > 5 && width > 4 ) {
        valid_rooms.push_back( room_dorm );
    }
    if( width > 8 ) {
        for( int i = 8; i < width; i += rng( 1, 2 ) ) {
            valid_rooms.push_back( room_split );
        }
    }

    point trap{ rng( p1.x + 1, p2.x - 1 ), rng( p1.y + 1, p2.y - 1 ) };
    switch( random_entry( valid_rooms ) ) {
        case room_closet:
            m->place_items( item_group_id( "cleaning" ), 80, p1, p2, false,
                            calendar::start_of_cataclysm );
            break;
        case room_lobby:
            if( rotate % 2 == 0 ) { // Vertical
                int desk = p1.y + rng( ( height / 2 ) - ( height / 4 ),
                                       ( height / 2 ) + 1 );
                for( int x = p1.x + ( width / 4 ); x < p2.x - ( width / 4 ); x++ ) {
                    m->furn_set( point( x, desk ), f_counter );
                }
                computer *tmpcomp = m->add_computer( tripoint( p2.x - ( width / 4 ), desk, z ),
                                                     _( "Log Console" ), 3 );
                tmpcomp->add_option( _( "View Research Logs" ), COMPACT_RESEARCH, 0 );
                tmpcomp->add_option( _( "Download Map Data" ), COMPACT_MAPS, 0 );
                tmpcomp->add_failure( COMPFAIL_SHUTDOWN );
                tmpcomp->add_failure( COMPFAIL_ALARM );
                tmpcomp->add_failure( COMPFAIL_DAMAGE );
                m->place_spawns( GROUP_TURRET, 1,
                                 point( ( ( p1.x + p2.x ) / 2 ), desk ),
                                 point( ( ( p1.x + p2.x ) / 2 ), desk ), 1, true );
            } else {
                int desk = p1.x + rng( ( height / 2 ) - ( height / 4 ),
                                       ( height / 2 ) + 1 );
                for( int y = p1.y + ( width / 4 ); y < p2.y - ( width / 4 ); y++ ) {
                    m->furn_set( point( desk, y ), f_counter );
                }
                computer *tmpcomp = m->add_computer( tripoint( desk, p2.y - ( width / 4 ), z ),
                                                     _( "Log Console" ), 3 );
                tmpcomp->add_option( _( "View Research Logs" ), COMPACT_RESEARCH, 0 );
                tmpcomp->add_option( _( "Download Map Data" ), COMPACT_MAPS, 0 );
                tmpcomp->add_failure( COMPFAIL_SHUTDOWN );
                tmpcomp->add_failure( COMPFAIL_ALARM );
                tmpcomp->add_failure( COMPFAIL_DAMAGE );
                m->place_spawns( GROUP_TURRET, 1,
                                 point( desk, ( ( p1.y + p2.y ) / 2 ) ),
                                 point( desk, ( ( p1.y + p2.y ) / 2 ) ), 1, true );
            }
            break;
        case room_chemistry:
            if( rotate % 2 == 0 ) { // Vertical
                for( int x = p1.x; x <= p2.x; x++ ) {
                    if( x % 3 == 0 ) {
                        for( int y = p1.y + 1; y <= p2.y - 1; y++ ) {
                            m->furn_set( point( x, y ), f_counter );
                        }
                        if( one_in( 3 ) ) {
                            m->place_items( item_group_id( "mut_lab" ), 35, point( x, p1.y + 1 ), point( x, p2.y - 1 ), false,
                                            calendar::start_of_cataclysm );
                        } else {
                            m->place_items( item_group_id( "chem_lab" ), 70, point( x, p1.y + 1 ), point( x, p2.y - 1 ), false,
                                            calendar::start_of_cataclysm );
                        }
                    }
                }
            } else {
                for( int y = p1.y; y <= p2.y; y++ ) {
                    if( y % 3 == 0 ) {
                        for( int x = p1.x + 1; x <= p2.x - 1; x++ ) {
                            m->furn_set( point( x, y ), f_counter );
                        }
                        if( one_in( 3 ) ) {
                            m->place_items( item_group_id( "mut_lab" ), 35, point( p1.x + 1, y ), point( p2.x - 1, y ), false,
                                            calendar::start_of_cataclysm );
                        } else {
                            m->place_items( item_group_id( "chem_lab" ), 70, point( p1.x + 1, y ), point( p2.x - 1, y ), false,
                                            calendar::start_of_cataclysm );
                        }
                    }
                }
            }
            break;
        case room_teleport:
            m->furn_set( point( ( p1.x + p2.x ) / 2, ( ( p1.y + p2.y ) / 2 ) ), f_counter );
            m->furn_set( point( ( ( p1.x + p2.x ) / 2 ) + 1,
                                ( ( p1.y + p2.y ) / 2 ) ),
                         f_counter );
            m->furn_set( point( ( p1.x + p2.x ) / 2, ( ( p1.y + p2.y ) / 2 ) + 1 ),
                         f_counter );
            m->furn_set( point( ( ( p1.x + p2.x ) / 2 ) + 1,
                                ( ( p1.y + p2.y ) / 2 ) + 1 ),
                         f_counter );
            mtrap_set( m, trap, tr_telepad );
            m->place_items( item_group_id( "teleport" ), 70, point( ( p1.x + p2.x ) / 2,
                            ( ( p1.y + p2.y ) / 2 ) ),
                            point( ( ( p1.x + p2.x ) / 2 ) + 1, ( ( p1.y + p2.y ) / 2 ) + 1 ),
                            false,
                            calendar::start_of_cataclysm );
            break;
        case room_goo:
            do {
                mtrap_set( m, trap, tr_goo );
                trap.x = rng( p1.x + 1, p2.x - 1 );
                trap.y = rng( p1.y + 1, p2.y - 1 );
            } while( !one_in( 5 ) );
            if( rotate == 0 ) {
                mremove_trap( m, point( p1.x, p2.y ) );
                m->furn_set( point( p1.x, p2.y ), f_freezer );
                m->place_items( item_group_id( "goo" ), 60, point( p1.x, p2.y ), point( p1.x, p2.y ), false,
                                calendar::start_of_cataclysm );
            } else if( rotate == 1 ) {
                mremove_trap( m, p1 );
                m->furn_set( p1, f_fridge );
                m->place_items( item_group_id( "goo" ), 60, p1, p1, false,
                                calendar::start_of_cataclysm );
            } else if( rotate == 2 ) {
                mremove_trap( m, point( p2.x, p1.y ) );
                m->furn_set( point( p2.x, p1.y ), f_freezer );
                m->place_items( item_group_id( "goo" ), 60, point( p2.x, p1.y ), point( p2.x, p1.y ), false,
                                calendar::start_of_cataclysm );
            } else {
                mremove_trap( m, p2 );
                m->furn_set( p2, f_fridge );
                m->place_items( item_group_id( "goo" ), 60, p2, p2, false,
                                calendar::start_of_cataclysm );
            }
            break;
        case room_cloning:
            for( int x = p1.x + 1; x <= p2.x - 1; x++ ) {
                for( int y = p1.y + 1; y <= p2.y - 1; y++ ) {
                    if( x % 3 == 0 && y % 3 == 0 ) {
                        m->ter_set( point( x, y ), t_vat );
                        m->place_items( item_group_id( "cloning_vat" ), 20, point( x, y ), point( x, y ), false,
                                        calendar::start_of_cataclysm );
                    }
                }
            }
            break;
        case room_vivisect:
            if( rotate == 0 ) {
                for( int x = p1.x; x <= p2.x; x++ ) {
                    m->furn_set( point( x, p2.y - 1 ), f_counter );
                }
                m->place_items( item_group_id( "dissection" ), 80, point( p1.x, p2.y - 1 ), p2 + point_north, false,
                                calendar::start_of_cataclysm );
            } else if( rotate == 1 ) {
                for( int y = p1.y; y <= p2.y; y++ ) {
                    m->furn_set( point( p1.x + 1, y ), f_counter );
                }
                m->place_items( item_group_id( "dissection" ), 80, p1 + point_east, point( p1.x + 1, p2.y ), false,
                                calendar::start_of_cataclysm );
            } else if( rotate == 2 ) {
                for( int x = p1.x; x <= p2.x; x++ ) {
                    m->furn_set( point( x, p1.y + 1 ), f_counter );
                }
                m->place_items( item_group_id( "dissection" ), 80, p1 + point_south, point( p2.x, p1.y + 1 ), false,
                                calendar::start_of_cataclysm );
            } else if( rotate == 3 ) {
                for( int y = p1.y; y <= p2.y; y++ ) {
                    m->furn_set( point( p2.x - 1, y ), f_counter );
                }
                m->place_items( item_group_id( "dissection" ), 80, point( p2.x - 1, p1.y ), p2 + point_west, false,
                                calendar::start_of_cataclysm );
            }
            mtrap_set( m, point( ( p1.x + p2.x ) / 2, ( ( p1.y + p2.y ) / 2 ) ),
                       tr_dissector );
            m->place_spawns( GROUP_LAB_CYBORG, 10,
                             point( ( ( ( p1.x + p2.x ) / 2 ) + 1 ),
                                    ( ( ( p1.y + p2.y ) / 2 ) + 1 ) ),
                             point( ( ( ( p1.x + p2.x ) / 2 ) + 1 ),
                                    ( ( ( p1.y + p2.y ) / 2 ) + 1 ) ), 1, true );
            break;

        case room_bionics:
            if( rotate % 2 == 0 ) {
                int biox = p1.x + 2;
                int bioy = ( ( p1.y + p2.y ) / 2 );
                mapf::formatted_set_simple( m, point( biox - 1, bioy - 1 ),
                                            "---\n"
                                            "|c|\n"
                                            "-=-\n",
                                            mapf::ter_bind( "- | =", t_concrete_wall, t_concrete_wall, t_reinforced_glass ),
                                            mapf::furn_bind( "c", f_counter ) );
                m->place_items( item_group_id( "bionics_common" ), 70, point( biox, bioy ), point( biox, bioy ),
                                false,
                                calendar::start_of_cataclysm );

                m->ter_set( point( biox, bioy + 2 ), t_console );
                computer *tmpcomp = m->add_computer( tripoint( biox,  bioy + 2, z ), _( "Bionic access" ), 2 );
                tmpcomp->add_option( _( "Manifest" ), COMPACT_LIST_BIONICS, 0 );
                tmpcomp->add_option( _( "Open Chambers" ), COMPACT_RELEASE_BIONICS, 3 );
                tmpcomp->add_failure( COMPFAIL_MANHACKS );
                tmpcomp->add_failure( COMPFAIL_SECUBOTS );
                tmpcomp->set_access_denied_msg(
                    _( "ERROR!  Access denied!  Unauthorized access will be met with lethal force!" ) );

                biox = p2.x - 2;
                mapf::formatted_set_simple( m, point( biox - 1, bioy - 1 ),
                                            "-=-\n"
                                            "|c|\n"
                                            "---\n",
                                            mapf::ter_bind( "- | =", t_concrete_wall, t_concrete_wall, t_reinforced_glass ),
                                            mapf::furn_bind( "c", f_counter ) );
                m->place_items( item_group_id( "bionics_common" ), 70, point( biox, bioy ), point( biox, bioy ),
                                false,
                                calendar::start_of_cataclysm );

                m->ter_set( point( biox, bioy - 2 ), t_console );
                computer *tmpcomp2 = m->add_computer( tripoint( biox,  bioy - 2, z ), _( "Bionic access" ), 2 );
                tmpcomp2->add_option( _( "Manifest" ), COMPACT_LIST_BIONICS, 0 );
                tmpcomp2->add_option( _( "Open Chambers" ), COMPACT_RELEASE_BIONICS, 3 );
                tmpcomp2->add_failure( COMPFAIL_MANHACKS );
                tmpcomp2->add_failure( COMPFAIL_SECUBOTS );
                tmpcomp2->set_access_denied_msg(
                    _( "ERROR!  Access denied!  Unauthorized access will be met with lethal force!" ) );
            } else {
                int bioy = p1.y + 2;
                int biox = ( ( p1.x + p2.x ) / 2 );
                mapf::formatted_set_simple( m, point( biox - 1, bioy - 1 ),
                                            "|-|\n"
                                            "|c=\n"
                                            "|-|\n",
                                            mapf::ter_bind( "- | =", t_concrete_wall, t_concrete_wall, t_reinforced_glass ),
                                            mapf::furn_bind( "c", f_counter ) );
                m->place_items( item_group_id( "bionics_common" ), 70, point( biox, bioy ), point( biox, bioy ),
                                false,
                                calendar::start_of_cataclysm );

                m->ter_set( point( biox + 2, bioy ), t_console );
                computer *tmpcomp = m->add_computer( tripoint( biox + 2,  bioy, z ), _( "Bionic access" ), 2 );
                tmpcomp->add_option( _( "Manifest" ), COMPACT_LIST_BIONICS, 0 );
                tmpcomp->add_option( _( "Open Chambers" ), COMPACT_RELEASE_BIONICS, 3 );
                tmpcomp->add_failure( COMPFAIL_MANHACKS );
                tmpcomp->add_failure( COMPFAIL_SECUBOTS );
                tmpcomp->set_access_denied_msg(
                    _( "ERROR!  Access denied!  Unauthorized access will be met with lethal force!" ) );

                bioy = p2.y - 2;
                mapf::formatted_set_simple( m, point( biox - 1, bioy - 1 ),
                                            "|-|\n"
                                            "=c|\n"
                                            "|-|\n",
                                            mapf::ter_bind( "- | =", t_concrete_wall, t_concrete_wall, t_reinforced_glass ),
                                            mapf::furn_bind( "c", f_counter ) );
                m->place_items( item_group_id( "bionics_common" ), 70, point( biox, bioy ), point( biox, bioy ),
                                false, calendar::start_of_cataclysm );

                m->ter_set( point( biox - 2, bioy ), t_console );
                computer *tmpcomp2 = m->add_computer( tripoint( biox - 2,  bioy, z ), _( "Bionic access" ), 2 );
                tmpcomp2->add_option( _( "Manifest" ), COMPACT_LIST_BIONICS, 0 );
                tmpcomp2->add_option( _( "Open Chambers" ), COMPACT_RELEASE_BIONICS, 3 );
                tmpcomp2->add_failure( COMPFAIL_MANHACKS );
                tmpcomp2->add_failure( COMPFAIL_SECUBOTS );
                tmpcomp2->set_access_denied_msg(
                    _( "ERROR!  Access denied!  Unauthorized access will be met with lethal force!" ) );
            }
            break;
        case room_dorm:
            if( rotate % 2 == 0 ) {
                for( int y = p1.y + 1; y <= p2.y - 1; y += 3 ) {
                    m->furn_set( point( p1.x, y ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( p1.x, y ), point( p1.x, y ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( p1.x + 1, y ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( p1.x + 1, y ), point( p1.x + 1, y ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( p2.x, y ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( p2.x, y ), point( p2.x, y ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( p2.x - 1, y ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( p2.x - 1, y ), point( p2.x - 1, y ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( p1.x, y + 1 ), f_dresser );
                    m->furn_set( point( p2.x, y + 1 ), f_dresser );
                    m->place_items( item_group_id( "dresser" ), 70, point( p1.x, y + 1 ), point( p1.x, y + 1 ), false,
                                    calendar::start_of_cataclysm );
                    m->place_items( item_group_id( "dresser" ), 70, point( p2.x, y + 1 ), point( p2.x, y + 1 ), false,
                                    calendar::start_of_cataclysm );
                }
            } else if( rotate % 2 == 1 ) {
                for( int x = p1.x + 1; x <= p2.x - 1; x += 3 ) {
                    m->furn_set( point( x, p1.y ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( x, p1.y ), point( x, p1.y ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( x, p1.y + 1 ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( x, p1.y + 1 ), point( x, p1.y + 1 ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( x, p2.y ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( x, p2.y ), point( x, p2.y ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( x, p2.y - 1 ), f_bed );
                    m->place_items( item_group_id( "bed" ), 60, point( x, p2.y - 1 ), point( x, p2.y - 1 ), false,
                                    calendar::start_of_cataclysm );
                    m->furn_set( point( x + 1, p1.y ), f_dresser );
                    m->furn_set( point( x + 1, p2.y ), f_dresser );
                    m->place_items( item_group_id( "dresser" ), 70, point( x + 1, p1.y ), point( x + 1, p1.y ), false,
                                    calendar::start_of_cataclysm );
                    m->place_items( item_group_id( "dresser" ), 70, point( x + 1, p2.y ), point( x + 1, p2.y ), false,
                                    calendar::start_of_cataclysm );
                }
            }
            m->place_items( item_group_id( "lab_dorm" ), 84, p1, p2, false,
                            calendar::start_of_cataclysm );
            break;
        case room_split:
            if( rotate % 2 == 0 ) {
                int w1 = ( ( p1.x + p2.x ) / 2 ) - 2;
                int w2 = ( ( p1.x + p2.x ) / 2 ) + 2;
                for( int y = p1.y; y <= p2.y; y++ ) {
                    m->ter_set( point( w1, y ), t_concrete_wall );
                    m->ter_set( point( w2, y ), t_concrete_wall );
                }
                m->ter_set( point( w1, ( ( p1.y + p2.y ) / 2 ) ), t_door_glass_frosted_c );
                m->ter_set( point( w2, ( ( p1.y + p2.y ) / 2 ) ), t_door_glass_frosted_c );
                science_room( m, p1, point( w1 - 1, p2.y ), z, 1 );
                science_room( m, point( w2 + 1, p1.y ), p2, z, 3 );
            } else {
                int w1 = ( ( p1.y + p2.y ) / 2 ) - 2;
                int w2 = ( ( p1.y + p2.y ) / 2 ) + 2;
                for( int x = p1.x; x <= p2.x; x++ ) {
                    m->ter_set( point( x, w1 ), t_concrete_wall );
                    m->ter_set( point( x, w2 ), t_concrete_wall );
                }
                m->ter_set( point( ( p1.x + p2.x ) / 2, w1 ), t_door_glass_frosted_c );
                m->ter_set( point( ( p1.x + p2.x ) / 2, w2 ), t_door_glass_frosted_c );
                science_room( m, p1, point( p2.x, w1 - 1 ), z, 2 );
                science_room( m, point( p1.x, w2 + 1 ), p2, z, 0 );
            }
            break;
        default:
            break;
    }
}

void map::create_anomaly( const tripoint &cp, artifact_natural_property prop, bool create_rubble )
{
    // TODO: Z
    point c( cp.xy() );
    if( create_rubble ) {
        rough_circle( this, t_dirt, c, 11 );
        rough_circle_furn( this, f_rubble, c, 5 );
        furn_set( c, f_null );
    }
    switch( prop ) {
        case ARTPROP_WRIGGLING:
        case ARTPROP_MOVING:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble ) {
                        add_field( {i, j, abs_sub.z}, fd_push_items, 1 );
                    }
                }
            }
            break;

        case ARTPROP_GLOWING:
        case ARTPROP_GLITTERING:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble && one_in( 2 ) ) {
                        mtrap_set( this, point( i, j ), tr_glow );
                    }
                }
            }
            break;

        case ARTPROP_HUMMING:
        case ARTPROP_RATTLING:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble && one_in( 2 ) ) {
                        mtrap_set( this, point( i, j ), tr_hum );
                    }
                }
            }
            break;

        case ARTPROP_WHISPERING:
        case ARTPROP_ENGRAVED:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble && one_in( 3 ) ) {
                        mtrap_set( this, point( i, j ), tr_shadow );
                    }
                }
            }
            break;

        case ARTPROP_BREATHING:
            for( int i = c.x - 1; i <= c.x + 1; i++ ) {
                for( int j = c.y - 1; j <= c.y + 1; j++ ) {
                    if( i == c.x && j == c.y ) {
                        place_spawns( GROUP_BREATHER_HUB, 1, point( i, j ), point( i, j ), 1,
                                      true );
                    } else {
                        place_spawns( GROUP_BREATHER, 1, point( i, j ), point( i, j ), 1, true );
                    }
                }
            }
            break;

        case ARTPROP_DEAD:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble ) {
                        mtrap_set( this, point( i, j ), tr_drain );
                    }
                }
            }
            break;

        case ARTPROP_ITCHY:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble ) {
                        set_radiation( point( i, j ), rng( 0, 10 ) );
                    }
                }
            }
            break;

        case ARTPROP_ELECTRIC:
        case ARTPROP_CRACKLING:
            add_field( {c, abs_sub.z}, fd_shock_vent, 3 );
            break;

        case ARTPROP_SLIMY:
            add_field( {c, abs_sub.z}, fd_acid_vent, 3 );
            break;

        case ARTPROP_WARM:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble ) {
                        add_field( {i, j, abs_sub.z}, fd_fire_vent, 1 + ( rl_dist( c, point( i, j ) ) % 3 ) );
                    }
                }
            }
            break;

        case ARTPROP_SCALED:
            for( int i = c.x - 5; i <= c.x + 5; i++ ) {
                for( int j = c.y - 5; j <= c.y + 5; j++ ) {
                    if( furn( point( i, j ) ) == f_rubble ) {
                        mtrap_set( this, point( i, j ), tr_snake );
                    }
                }
            }
            break;

        case ARTPROP_FRACTAL:
            create_anomaly( c + point( -4, -4 ),
                            static_cast<artifact_natural_property>( rng( ARTPROP_NULL + 1, ARTPROP_MAX - 1 ) ) );
            create_anomaly( c + point( 4, -4 ),
                            static_cast<artifact_natural_property>( rng( ARTPROP_NULL + 1, ARTPROP_MAX - 1 ) ) );
            create_anomaly( c + point( -4, 4 ),
                            static_cast<artifact_natural_property>( rng( ARTPROP_NULL + 1, ARTPROP_MAX - 1 ) ) );
            create_anomaly( c + point( 4, -4 ),
                            static_cast<artifact_natural_property>( rng( ARTPROP_NULL + 1, ARTPROP_MAX - 1 ) ) );
            break;
        default:
            break;
    }
}
///////////////////// part of map

void line( map *m, const ter_id &type, point p1, point p2 )
{
    m->draw_line_ter( type, p1, p2 );
}
void line_furn( map *m, const furn_id &type, point p1, point p2 )
{
    m->draw_line_furn( type, p1, p2 );
}
void fill_background( map *m, const ter_id &type )
{
    m->draw_fill_background( type );
}
void fill_background( map *m, ter_id( *f )() )
{
    m->draw_fill_background( f );
}
void square( map *m, const ter_id &type, point p1, point p2 )
{
    m->draw_square_ter( type, p1, p2 );
}
void square_furn( map *m, const furn_id &type, point p1, point p2 )
{
    m->draw_square_furn( type, p1, p2 );
}
void square( map *m, ter_id( *f )(), point p1, point p2 )
{
    m->draw_square_ter( f, p1, p2 );
}
void square( map *m, const weighted_int_list<ter_id> &f, point p1, point p2 )
{
    m->draw_square_ter( f, p1, p2 );
}
void rough_circle( map *m, const ter_id &type, point p, int rad )
{
    m->draw_rough_circle_ter( type, p, rad );
}
void rough_circle_furn( map *m, const furn_id &type, point p, int rad )
{
    m->draw_rough_circle_furn( type, p, rad );
}
void circle( map *m, const ter_id &type, double x, double y, double rad )
{
    m->draw_circle_ter( type, rl_vec2d( x, y ), rad );
}
void circle( map *m, const ter_id &type, point p, int rad )
{
    m->draw_circle_ter( type, p, rad );
}
void circle_furn( map *m, const furn_id &type, point p, int rad )
{
    m->draw_circle_furn( type, p, rad );
}
void add_corpse( map *m, point p )
{
    m->add_corpse( tripoint( p, m->get_abs_sub().z ) );
}

//////////////////// mapgen update
update_mapgen_function_json::update_mapgen_function_json( const json_source_location &jsrcloc ) :
    mapgen_function_json_base( jsrcloc )
{
}

void update_mapgen_function_json::check( const std::string &oter_name ) const
{
    check_common( oter_name );
}

bool update_mapgen_function_json::setup_update( const JsonObject &jo )
{
    return setup_common( jo );
}

bool update_mapgen_function_json::setup_internal( const JsonObject &/*jo*/ )
{
    fill_ter = t_null;
    /* update_mapgen doesn't care about fill_ter or rows */
    return true;
}

bool update_mapgen_function_json::update_map( const tripoint_abs_omt &omt_pos, point offset,
        mission *miss, bool verify ) const
{
    if( omt_pos == overmap::invalid_tripoint ) {
        debugmsg( "Mapgen update function called with overmap::invalid_tripoint" );
        return false;
    }
    tinymap update_tmap;
    // TODO: fix point types
    const tripoint sm_pos = project_to<coords::sm>( omt_pos ).raw();
    update_tmap.load( sm_pos, true );

    mapgendata md( omt_pos, update_tmap, 0.0f, calendar::start_of_cataclysm, miss );

    return update_map( md, offset, verify );
}

bool update_mapgen_function_json::update_map( const mapgendata &md, const point &offset,
        const bool verify ) const
{
    mapgendata md_with_params( md, get_args( md, mapgen_parameter_scope::omt ) );

    class rotation_guard
    {
        public:
            rotation_guard( const mapgendata &md )
                : md( md ), rotation( oter_get_rotations( md.terrain_type() ) ) {
                // If the existing map is rotated, we need to rotate it back to the north
                // orientation before applying our updates.
                if( rotation != 0 ) {
                    md.m.rotate( 4 - rotation, true );
                }
            }

            ~rotation_guard() {
                // If we rotated the map before applying updates, we now need to rotate
                // it back to where we found it.
                if( rotation != 0 ) {
                    md.m.rotate( rotation, true );
                }
            }
        private:
            const mapgendata &md;
            const int rotation;
    };
    rotation_guard rot( md_with_params );

    for( const jmapgen_setmap &elem : setmap_points ) {
        if( verify && elem.has_vehicle_collision( md_with_params, offset ) ) {
            return false;
        }
        elem.apply( md_with_params, offset );
    }

    if( verify && objects.has_vehicle_collision( md_with_params, offset ) ) {
        return false;
    }
    objects.apply( md_with_params, offset );

    resolve_regional_terrain_and_furniture( md_with_params );

    return true;
}

mapgen_update_func add_mapgen_update_func( const JsonObject &jo, bool &defer )
{
    if( jo.has_string( "mapgen_update_id" ) ) {
        const std::string mapgen_update_id = jo.get_string( "mapgen_update_id" );
        const auto update_function = [mapgen_update_id]( const tripoint_abs_omt & omt_pos,
        mission * miss ) {
            run_mapgen_update_func( mapgen_update_id, omt_pos, miss, false );
        };
        return update_function;
    }

    update_mapgen_function_json json_data( json_source_location{} );
    mapgen_defer::defer = defer;
    if( !json_data.setup_update( jo ) ) {
        const auto null_function = []( const tripoint_abs_omt &, mission * ) {
        };
        return null_function;
    }
    const auto update_function = [json_data]( const tripoint_abs_omt & omt_pos, mission * miss ) {
        json_data.update_map( omt_pos, point_zero, miss );
    };
    defer = mapgen_defer::defer;
    mapgen_defer::jsi = JsonObject();
    return update_function;
}

bool run_mapgen_update_func( const std::string &update_mapgen_id, const tripoint_abs_omt &omt_pos,
                             mission *miss, bool cancel_on_collision )
{
    const auto update_function = update_mapgen.find( update_mapgen_id );

    if( update_function == update_mapgen.end() || update_function->second.empty() ) {
        return false;
    }
    return update_function->second[0]->update_map( omt_pos, point_zero, miss, cancel_on_collision );
}

bool run_mapgen_update_func( const std::string &update_mapgen_id, mapgendata &dat,
                             const bool cancel_on_collision )
{
    const auto update_function = update_mapgen.find( update_mapgen_id );
    if( update_function == update_mapgen.end() || update_function->second.empty() ) {
        return false;
    }
    return update_function->second[0]->update_map( dat, point_zero, cancel_on_collision );
}

std::pair<std::map<ter_id, int>, std::map<furn_id, int>> get_changed_ids_from_update(
            const std::string &update_mapgen_id )
{
    const int fake_map_z = -9;

    std::map<ter_id, int> terrains;
    std::map<furn_id, int> furnitures;

    const auto update_function = update_mapgen.find( update_mapgen_id );

    if( !update_mapgen_id.empty() && update_function == update_mapgen.end() ) {
        debugmsg( "Couldn't find mapgen function with id %s", update_mapgen_id );
        return std::make_pair( terrains, furnitures );
    }

    if( update_mapgen_id.empty() || update_function->second.empty() ) {
        return std::make_pair( terrains, furnitures );
    }

    ::fake_map fake_map( f_null, t_dirt, tr_null, fake_map_z );

    mapgendata fake_md( fake_map, mapgendata::dummy_settings );

    if( update_function->second[0]->update_map( fake_md ) ) {
        for( const tripoint &pos : fake_map.points_on_zlevel( fake_map_z ) ) {
            ter_id ter_at_pos = fake_map.ter( pos );
            if( ter_at_pos != t_dirt ) {
                terrains[ter_at_pos] += 1;
            }
            if( fake_map.has_furn( pos ) ) {
                furn_id furn_at_pos = fake_map.furn( pos );
                furnitures[furn_at_pos] += 1;
            }
        }
    }
    return std::make_pair( terrains, furnitures );
}

bool run_mapgen_func( const std::string &mapgen_id, mapgendata &dat )
{
    return oter_mapgen.generate( dat, mapgen_id );
}

mapgen_parameters get_map_special_params( const std::string &mapgen_id )
{
    return oter_mapgen.get_map_special_params( mapgen_id );
}

int register_mapgen_function( const std::string &key )
{
    if( const auto ptr = get_mapgen_cfunction( key ) ) {
        return oter_mapgen.add( key, std::make_shared<mapgen_function_builtin>( ptr ) );
    }
    return -1;
}

bool has_mapgen_for( const std::string &key )
{
    return oter_mapgen.has( key );
}

namespace mapgen
{

bool has_update_id( const mapgen_id &id )
{
    return update_mapgen.find( id ) != update_mapgen.end();
}

} // namespace mapgen
